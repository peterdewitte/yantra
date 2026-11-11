// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The console of a windowed program (NFR-09, HW-06). On Windows yantra.exe
// is a GUI program (no console window opens when it is started from the
// Start menu or Explorer), so it has no standard output of its own: when it
// is started from a console (cmd, PowerShell) without redirection,
// attach_parent_console() sends standard output and error to that console,
// so --help, --version and the log lines show there. Redirected streams
// (pipes, files: the tests) stay as they are. Elsewhere: no-op.
#pragma once

namespace yantra::platform {

void attach_parent_console();

}  // namespace yantra::platform
