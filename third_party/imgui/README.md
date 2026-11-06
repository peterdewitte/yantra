<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Dear ImGui 1.92.9b (vendored)

The core files, `imstb_*.h`, `LICENSE.txt` and the GLFW and OpenGL 3
backends (`backends/imgui_impl_{glfw,opengl3}.*` with
`imgui_impl_opengl3_loader.h`) of https://github.com/ocornut/imgui,
release tag `v1.92.9b`, unchanged (MIT; `imstb_*.h` MIT OR Unlicense;
the gl3w-generated OpenGL loader public domain).

- Source: https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
- SHA-256 of the archive: 21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
- SHA-256 of `imgui.cpp`: 5a1e5128c0305f50b6556a77f71fde35836505883b74c1d6ae44723243f1e8de
- SHA-256 of `imgui.h`: 0d8db1045db01d908853adfd26ae07c5bc5ab4789d4515f6ea34234a69ade0ca
- SHA-256 of `LICENSE.txt`: 173506a2d6f7fb67990d257fb2507f188690eca39060c39469ae7bef43aae2a3

Used by the desktop app only (UI-13, OA-17, spec 16.2): `app/CMakeLists.txt`
builds the core files into `yantra_imgui` (no window system or OpenGL
needed, also used by the headless UI tests) and the two backends into
`yantra_imgui_backends` (GLFW, OpenGL 3). Built with
`IMGUI_ENABLE_TEST_ENGINE`, whose item hooks Yantra implements itself in
`app/gui/ui_probe.cpp` (no Dear ImGui Test Engine code is used).
