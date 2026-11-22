# Security Policy

## Reporting a vulnerability

Please report security vulnerabilities **privately** via GitHub's
[private vulnerability reporting](https://github.com/FuhMhanShu/FuhGlGraphics/security/advisories/new).
Do **not** open a public issue, pull request or discussion for a security bug.

Include, if possible:

- the affected version/commit and platform (OS, GPU/driver, GL version),
- a description of the issue and its security impact,
- a minimal reproduction (code, input file or input data that triggers it).

You can expect an acknowledgement within 72 hours. For confirmed issues we aim
to publish a fix or mitigation within 30 days and credit the reporter (unless
they prefer to stay anonymous).

## Supported versions

Security fixes are applied to the current state of `main` and released with the
next version (see [CHANGELOG.md](CHANGELOG.md); the project is currently in the
`0.x` line, so there is no older release line receiving fixes).

## Scope

FuhGlGraphics is a client-side graphics library embedded into an application. The
glTF 2.0 asset loader (G4) and the JSON config/asset-cache parsers (G4/G11)
process **untrusted input** (arbitrary `.gltf`/`.glb`/JSON files), so
memory-safety bugs there — out-of-bounds reads/writes, use-after-free, unbounded
recursion or unbounded memory growth in the parsers — are always treated as
vulnerabilities. The same applies to any memory-safety or robustness bug in
library code itself. A bug that requires the host application to feed the
library input it already fully controls (e.g. a hand-written shader string) is
generally a bug in the host application, not a vulnerability of this library.
