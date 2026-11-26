#!/usr/bin/env python3
"""Unit tests for tools/asset_creator/mcp_server.py (stdlib unittest, no MCP SDK).

Spawns the server as a subprocess and talks line-delimited JSON-RPC 2.0 over
stdio, exactly like the asset creator's McpClient will. Run:

    python3 tools/asset_creator/test_mcp_server.py
"""

import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER = os.path.join(HERE, "mcp_server.py")


def make_glb(path):
    """Writes a minimal .glb fixture (one mesh, one material, two nodes)."""
    gltf = {
        "asset": {"version": "2.0", "generator": "test"},
        "meshes": [{"name": "quad", "primitives": [{"attributes": {"POSITION": 0}}]}],
        "materials": [{"name": "red"}],
        "nodes": [{"name": "root"}, {"name": "quad-node", "mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "animations": [{"name": "spin"}],
        "extensionsUsed": ["KHR_materials_pbrMetallicRoughness"],
    }
    js = json.dumps(gltf).encode("utf-8")
    pad = (4 - len(js) % 4) % 4
    js += b" " * pad
    total = 12 + 8 + len(js)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<I4s", len(js), b"JSON"))
        f.write(js)


class McpServerProcess:
    def __init__(self, extra_env=None):
        env = dict(os.environ)
        env.pop("MISTRAL_API_KEY", None)  # force the offline stub
        if extra_env:
            env.update(extra_env)
        self.proc = subprocess.Popen(
            [sys.executable, SERVER], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, env=env, bufsize=1)
        self._next_id = 0

    def call(self, method, params=None):
        self._next_id += 1
        rid = self._next_id
        req = {"jsonrpc": "2.0", "id": rid, "method": method}
        if params is not None:
            req["params"] = params
        self.proc.stdin.write(json.dumps(req) + "\n")
        self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        assert line, "server closed stdout"
        resp = json.loads(line)
        assert resp["id"] == rid, f"id mismatch: {resp}"
        return resp

    def notify(self, method, params=None):
        req = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            req["params"] = params
        self.proc.stdin.write(json.dumps(req) + "\n")
        self.proc.stdin.flush()

    def close(self):
        self.proc.stdin.close()
        self.proc.stdout.close()
        self.proc.wait(timeout=10)


class TestMcpServer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.srv = McpServerProcess()
        init = cls.srv.call("initialize", {"protocolVersion": "2024-11-05",
                                           "capabilities": {}, "clientInfo": {"name": "test"}})
        assert "result" in init, init
        assert init["result"]["serverInfo"]["name"] == "fuhglgraphics-asset-creator-dev"
        cls.srv.notify("notifications/initialized")

    @classmethod
    def tearDownClass(cls):
        cls.srv.close()

    def test_tools_list(self):
        resp = self.srv.call("tools/list")
        names = [t["name"] for t in resp["result"]["tools"]]
        for expected in ("chat", "list_assets", "inspect_gltf", "render_preview",
                         "asset_action", "editor_command"):
            self.assertIn(expected, names)

    def test_inspect_glb(self):
        with tempfile.TemporaryDirectory() as td:
            glb = os.path.join(td, "fixture.glb")
            make_glb(glb)
            resp = self.srv.call("tools/call", {"name": "inspect_gltf",
                                                "arguments": {"path": glb}})
            text = resp["result"]["content"][0]["text"]
            data = json.loads(text)
            self.assertFalse(resp["result"].get("isError", False))
            self.assertEqual(data["meshes"], 1)
            self.assertEqual(data["materials"], 1)
            self.assertEqual(data["nodes"], ["root", "quad-node"])
            self.assertEqual(data["animations"], 1)
            self.assertIn("KHR_materials_pbrMetallicRoughness", data["extensionsUsed"])

    def test_inspect_gltf_json(self):
        with tempfile.TemporaryDirectory() as td:
            p = os.path.join(td, "a.gltf")
            with open(p, "w") as f:
                json.dump({"asset": {"version": "2.0"}, "meshes": [{}, {}]}, f)
            resp = self.srv.call("tools/call", {"name": "inspect_gltf",
                                                "arguments": {"path": p}})
            data = json.loads(resp["result"]["content"][0]["text"])
            self.assertEqual(data["meshes"], 2)

    def test_list_assets(self):
        with tempfile.TemporaryDirectory() as td:
            for n in ("ship.glb", "crate.gltf", "wood.png", "notes.txt"):
                open(os.path.join(td, n), "w").close()
            resp = self.srv.call("tools/call", {"name": "list_assets",
                                                "arguments": {"directory": td}})
            data = json.loads(resp["result"]["content"][0]["text"])
            self.assertEqual(data["files"], ["crate.gltf", "ship.glb", "wood.png"])

    def test_chat_offline_stub(self):
        resp = self.srv.call("tools/call", {"name": "chat",
                                            "arguments": {"message": "hello"}})
        text = resp["result"]["content"][0]["text"]
        self.assertIn("offline stub", text)
        self.assertIn("hello", text)
        self.assertFalse(resp["result"].get("isError", False))

    def test_asset_action_bridge(self):
        resp = self.srv.call("tools/call", {"name": "asset_action",
                                            "arguments": {"op": "add_node",
                                                          "params": {"name": "box",
                                                                     "translation": [1, 0, 0]}}})
        result = resp["result"]
        self.assertEqual(result["structuredContent"]["assetAction"]["op"], "add_node")
        self.assertIn("assetAction", json.loads(result["content"][0]["text"]))

    def test_editor_command_bridge_file(self):
        with tempfile.TemporaryDirectory() as td:
            bridge = os.path.join(td, "bridge.jsonl")
            srv = McpServerProcess(extra_env={"EDITOR_BRIDGE_FILE": bridge})
            try:
                srv.call("initialize")
                resp = srv.call("tools/call", {"name": "editor_command",
                                               "arguments": {"command": "select",
                                                             "args": {"node": "ship"}}})
                self.assertFalse(resp["result"].get("isError", False))
                with open(bridge) as f:
                    line = json.loads(f.readline())
                self.assertEqual(line, {"command": "select", "args": {"node": "ship"}})
            finally:
                srv.close()

    def test_unknown_method_and_tool(self):
        resp = self.srv.call("bogus/method")
        self.assertIn("error", resp)
        resp = self.srv.call("tools/call", {"name": "nope", "arguments": {}})
        self.assertTrue(resp["result"].get("isError", False))

    def test_render_preview_without_binary(self):
        # Without a built fuhglgraphics_gltf_demo the tool must fail gracefully.
        if os.environ.get("FUHGLGRAPHICS_GLTF_DEMO") or \
                os.path.exists(os.path.join(HERE, "..", "..", "build", "examples",
                                            "fuhglgraphics_gltf_demo")):
            self.skipTest("binary present — graceful-failure path not reachable")
        with tempfile.TemporaryDirectory() as td:
            glb = os.path.join(td, "f.glb")
            make_glb(glb)
            resp = self.srv.call("tools/call", {"name": "render_preview",
                                                "arguments": {"model": glb, "out": os.path.join(td, "o.png")}})
            self.assertTrue(resp["result"].get("isError", False))
            self.assertIn("binary not found", resp["result"]["content"][0]["text"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
