#!/usr/bin/env python3
"""Reference MCP server (dev) for the FuhGlGraphics asset creator (G12).

Python 3 standard library only — no pip installs, mirroring the repo rule
(docs/REQUIREMENTS.md N1) that no third-party dependencies are added.

What it does
------------
Speaks MCP over **stdio** (line-delimited JSON-RPC 2.0, the MCP stdio
transport) so the asset creator's chat panel can connect to it with the
server command:

    python3 tools/asset_creator/mcp_server.py

and exposes tools that combine LLM chat with FuhGlGraphics features:

- ``chat``            LLM chat via the Mistral API (``MISTRAL_API_KEY`` /
                      ``MISTRAL_MODEL`` from the environment). Without a key it
                      answers with an explanatory stub, so the wiring can be
                      tested offline.
- ``list_assets``     Lists .gltf/.glb/.png/.fasset files of a directory.
- ``inspect_gltf``    Parses the .gltf JSON / .glb header + JSON chunk and
                      reports meshes/materials/images/nodes/animations/skins
                      (same data the G4 ``io::loadGltf`` produces).
- ``render_preview``  Runs the compiled ``fuhglgraphics_gltf_demo`` example
                      headless (surfaceless EGL, like the repo's CI) through the
                      G3 DeferredRenderer and returns the PNG as MCP image
                      content — a real render from FuhGlGraphics.
- ``asset_action``    Returns an ``assetAction`` JSON object (add_node,
                      set_transform, set_material, load_gltf, ...) that the
                      editor's ChatController maps onto QUndoCommands — this
                      is how the LLM steers the editor.
- ``editor_command``  Appends a command line to an editor bridge file
                      (``EDITOR_BRIDGE_FILE``, default ./editor_bridge.jsonl)
                      which the Qt tool can watch with QFileSystemWatcher for
                      direct (non-chat) control.

Optional loopback HTTP mode for direct editor control (no stdio client):

    python3 tools/asset_creator/mcp_server.py --http 8765
    GET  http://127.0.0.1:8765/health
    GET  http://127.0.0.1:8765/tools
    POST http://127.0.0.1:8765/call   {"name": "inspect_gltf", "arguments": {"path": "..."}}

Only 127.0.0.1 is bound; no credentials are ever written to disk — the
Mistral key stays in the process environment.

Logging goes to stderr (stdout carries JSON-RPC only).
"""

import base64
import json
import os
import shutil
import struct
import subprocess
import sys
import urllib.request

SERVER_NAME = "fuhglgraphics-asset-creator-dev"
SERVER_VERSION = "0.1.0"
PROTOCOL_VERSION = "2024-11-05"

MISTRAL_API_URL = "https://api.mistral.ai/v1/chat/completions"
MISTRAL_MODEL = os.environ.get("MISTRAL_MODEL", "mistral-small-latest")

TOOLS = [
    {
        "name": "chat",
        "description": "Chat with the LLM (Mistral API). The editor shows the reply in the chat panel.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "message": {"type": "string", "description": "User message"},
                "history": {
                    "type": "array",
                    "description": "Optional prior messages [{role, content}]",
                    "items": {"type": "object"},
                },
            },
            "required": ["message"],
        },
    },
    {
        "name": "list_assets",
        "description": "List asset files (.gltf/.glb/.png/.jpg/.fasset) in a directory.",
        "inputSchema": {
            "type": "object",
            "properties": {"directory": {"type": "string"}},
            "required": ["directory"],
        },
    },
    {
        "name": "inspect_gltf",
        "description": "Parse a .gltf/.glb and report meshes, materials, images, nodes, animations, skins.",
        "inputSchema": {
            "type": "object",
            "properties": {"path": {"type": "string"}},
            "required": ["path"],
        },
    },
    {
        "name": "render_preview",
        "description": "Render a glTF model headless through the FuhGlGraphics G3 renderer "
        "(runs the compiled fuhglgraphics_gltf_demo example) and return the PNG.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "model": {"type": "string", "description": ".gltf/.glb path"},
                "out": {"type": "string", "description": "PNG output path (default preview.png)"},
                "width": {"type": "integer", "default": 1280},
                "height": {"type": "integer", "default": 720},
                "no_shadows": {"type": "boolean", "default": False},
            },
            "required": ["model"],
        },
    },
    {
        "name": "asset_action",
        "description": "Return an assetAction JSON the editor applies as an undoable command "
        "(LLM -> editor bridge).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "op": {"type": "string", "enum": ["add_node", "remove_node", "set_transform",
                                                 "set_material", "load_gltf", "focus_node",
                                                 "play_animation", "stop_animation"]},
                "params": {"type": "object"},
            },
            "required": ["op", "params"],
        },
    },
    {
        "name": "editor_command",
        "description": "Append a raw command for the editor to a bridge file "
        "(EDITOR_BRIDGE_FILE) that the Qt tool watches.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "command": {"type": "string"},
                "args": {"type": "object", "default": {}},
            },
            "required": ["command"],
        },
    },
]


def log(msg):
    print(f"[{SERVER_NAME}] {msg}", file=sys.stderr, flush=True)


# ---------------------------------------------------------------- Mistral chat
def mistral_chat(message, history):
    key = os.environ.get("MISTRAL_API_KEY", "").strip()
    if not key:
        return ("(offline stub — set MISTRAL_API_KEY to enable real chat) "
                "I received: " + message), False
    messages = [{"role": m.get("role", "user"), "content": m.get("content", "")}
                for m in (history or []) if isinstance(m, dict)]
    messages.append({"role": "user", "content": message})
    payload = json.dumps({"model": MISTRAL_MODEL, "messages": messages}).encode("utf-8")
    req = urllib.request.Request(
        MISTRAL_API_URL, data=payload,
        headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            data = json.loads(r.read().decode("utf-8"))
        return data["choices"][0]["message"]["content"], False
    except Exception as e:  # noqa: BLE001 - report any API error to the chat panel
        return f"Mistral API error: {e}", True


# ------------------------------------------------------------------- glTF read
def read_gltf_json(path):
    """Returns the glTF JSON dict of a .gltf (JSON file) or .glb (binary chunk)."""
    with open(path, "rb") as f:
        head = f.read(12)
        if head[:4] == b"glTF":  # .glb container
            _, _, length = struct.unpack("<4sII", head)
            chunk_len, chunk_type = struct.unpack("<I4s", f.read(8))
            if chunk_type != b"JSON":
                raise ValueError("GLB first chunk is not JSON")
            return json.loads(f.read(chunk_len).decode("utf-8"))
        f.seek(0)
        return json.loads(f.read().decode("utf-8"))


def inspect_gltf(path):
    if not os.path.isfile(path):
        return {"error": f"not a file: {path}"}, True
    try:
        g = read_gltf_json(path)
    except Exception as e:  # noqa: BLE001
        return {"error": str(e)}, True
    return {
        "file": path,
        "generator": g.get("generator", {}).get("name", ""),
        "meshes": len(g.get("meshes", [])),
        "materials": len(g.get("materials", [])),
        "images": len(g.get("images", [])),
        "textures": len(g.get("textures", [])),
        "nodes": [n.get("name", f"node{i}") for i, n in enumerate(g.get("nodes", []))],
        "animations": len(g.get("animations", [])),
        "skins": len(g.get("skins", [])),
        "scenes": len(g.get("scenes", [])),
        "extensionsUsed": g.get("extensionsUsed", []),
    }, False


def list_assets(directory):
    if not os.path.isdir(directory):
        return {"error": f"not a directory: {directory}"}, True
    exts = (".gltf", ".glb", ".png", ".jpg", ".jpeg", ".fasset")
    files = sorted(f for f in os.listdir(directory) if f.lower().endswith(exts))
    return {"directory": directory, "files": files}, False


# -------------------------------------------------------------- render preview
def find_demo_binary():
    """Locate the compiled fuhglgraphics_gltf_demo example binary."""
    env = os.environ.get("FUHGLGRAPHICS_GLTF_DEMO")
    if env and os.path.isfile(env):
        return env
    found = shutil.which("fuhglgraphics_gltf_demo")
    if found:
        return found
    here = os.path.dirname(os.path.abspath(__file__))
    for cand in (os.path.join(here, "..", "..", "build", "examples", "fuhglgraphics_gltf_demo"),
                 os.path.join(here, "..", "..", "build", "fuhglgraphics_gltf_demo")):
        if os.path.isfile(cand):
            return os.path.abspath(cand)
    return None


def render_preview(model, out="preview.png", width=1280, height=720, no_shadows=False):
    binary = find_demo_binary()
    if not binary:
        return {"error": "fuhglgraphics_gltf_demo binary not found — build the repo "
                         "(cmake build with FUHGLGRAPHICS_BUILD_EXAMPLES=ON) or set "
                         "FUHGLGRAPHICS_GLTF_DEMO to the binary path"}, True
    if not os.path.isfile(model):
        return {"error": f"model not found: {model}"}, True
    env = dict(os.environ)
    env.setdefault("FUHGL_BACKEND", "surfaceless")  # headless EGL, like the repo CI
    cmd = [binary, "--model", model, "--out", out,
           "--width", str(int(width)), "--height", str(int(height))]
    if no_shadows:
        cmd.append("--no-shadows")
    proc = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=300)
    if proc.returncode != 0 or not os.path.isfile(out):
        return {"error": f"render failed (exit {proc.returncode})",
                "stderr": proc.stderr[-2000:]}, True
    with open(out, "rb") as f:
        png_b64 = base64.b64encode(f.read()).decode("ascii")
    return {"out": os.path.abspath(out), "png_base64": png_b64,
            "width": int(width), "height": int(height)}, False


# ------------------------------------------------------------------ tool calls
def call_tool(name, arguments):
    """Returns (result_dict, is_error); result_dict maps to MCP content."""
    a = arguments or {}
    if name == "chat":
        text, err = mistral_chat(a.get("message", ""), a.get("history"))
        return {"content": [{"type": "text", "text": text}], "isError": err}, err
    if name == "list_assets":
        res, err = list_assets(a.get("directory", "."))
        return {"content": [{"type": "text", "text": json.dumps(res, indent=2)}], "isError": err}, err
    if name == "inspect_gltf":
        res, err = inspect_gltf(a.get("path", ""))
        return {"content": [{"type": "text", "text": json.dumps(res, indent=2)}], "isError": err}, err
    if name == "render_preview":
        res, err = render_preview(a.get("model", ""), a.get("out", "preview.png"),
                                  a.get("width", 1280), a.get("height", 720),
                                  bool(a.get("no_shadows", False)))
        content = [{"type": "text", "text": json.dumps({k: v for k, v in res.items()
                                                        if k != "png_base64"}, indent=2)}]
        if not err and res.get("png_base64"):
            content.append({"type": "image", "data": res["png_base64"], "mimeType": "image/png"})
        return {"content": content, "isError": err}, err
    if name == "asset_action":
        action = {"op": a.get("op", ""), "params": a.get("params", {})}
        return {"content": [{"type": "text", "text": json.dumps({"assetAction": action}, indent=2)}],
                "structuredContent": {"assetAction": action}, "isError": False}, False
    if name == "editor_command":
        bridge = os.environ.get("EDITOR_BRIDGE_FILE", "editor_bridge.jsonl")
        line = json.dumps({"command": a.get("command", ""), "args": a.get("args", {})})
        with open(bridge, "a", encoding="utf-8") as f:
            f.write(line + "\n")
        return {"content": [{"type": "text", "text": f"queued in {bridge}: {line}"}]}, False
    return {"content": [{"type": "text", "text": f"unknown tool: {name}"}], "isError": True}, True


# ------------------------------------------------------------------ JSON-RPC
def handle(req):
    method = req.get("method", "")
    rid = req.get("id")
    if method.startswith("notifications/"):
        return None
    if method == "initialize":
        return {"protocolVersion": PROTOCOL_VERSION,
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION}}
    if method == "ping":
        return {}
    if method == "tools/list":
        return {"tools": TOOLS}
    if method == "tools/call":
        params = req.get("params", {})
        result, _err = call_tool(params.get("name", ""), params.get("arguments", {}))
        return result
    if method == "resources/list":
        return {"resources": []}
    if method == "prompts/list":
        return {"prompts": []}
    return {"__error__": {"code": -32601, "message": f"method not found: {method}"}}


def serve_stdio():
    log(f"stdio MCP server ready (python {sys.version.split()[0]}, tools: {len(TOOLS)})")
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError as e:
            resp = {"jsonrpc": "2.0", "id": None,
                    "error": {"code": -32700, "message": f"parse error: {e}"}}
        else:
            result = handle(req)
            if result is None:
                continue  # notification
            if "__error__" in result:
                resp = {"jsonrpc": "2.0", "id": req.get("id"), "error": result["__error__"]}
            else:
                resp = {"jsonrpc": "2.0", "id": req.get("id"), "result": result}
        sys.stdout.write(json.dumps(resp) + "\n")
        sys.stdout.flush()


def serve_http(port):
    """Loopback-only HTTP transport for direct editor control (no stdio client)."""
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

    class Handler(BaseHTTPRequestHandler):
        def _send(self, code, obj):
            body = json.dumps(obj).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):  # noqa: N802
            if self.path == "/health":
                self._send(200, {"ok": True, "server": SERVER_NAME, "version": SERVER_VERSION})
            elif self.path == "/tools":
                self._send(200, {"tools": TOOLS})
            else:
                self._send(404, {"error": "not found"})

        def do_POST(self):  # noqa: N802
            if self.path != "/call":
                self._send(404, {"error": "not found"})
                return
            try:
                length = int(self.headers.get("Content-Length", 0))
                req = json.loads(self.rfile.read(length).decode("utf-8"))
                result, _err = call_tool(req.get("name", ""), req.get("arguments", {}))
                self._send(200, result)
            except Exception as e:  # noqa: BLE001
                self._send(400, {"error": str(e)})

        def log_message(self, *args):
            pass

    srv = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    log(f"HTTP loopback on http://127.0.0.1:{port} (/health /tools POST /call)")
    srv.serve_forever()


if __name__ == "__main__":
    if "--http" in sys.argv:
        port = 8765
        i = sys.argv.index("--http")
        if i + 1 < len(sys.argv) and sys.argv[i + 1].isdigit():
            port = int(sys.argv[i + 1])
        serve_http(port)
    else:
        serve_stdio()
