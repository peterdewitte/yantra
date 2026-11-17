<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

![Yantra](../img/yantra_logo_128.png)

# Yantra user manual

Yantra measures TETRA coverage inside buildings, floor by floor: import the floor plans, scale and line them up, walk the floors with a radio, and get coverage maps and an acceptance report. Yantra is made by curtat.be (https://curtat.be) as a reference on building new programs with Claude, Anthropic's AI assistant.

This manual describes Yantra as it is today. It is installed with the app and opened from the Help button of every window, with F1 and from Help > User manual, so it works offline. Each section has a number, the number the in-app help names ("See manual 13.2").

Start here:

- [Quick starts](../quickstart/README.md): one page each, "first indoor project", "indoor walk test with acceptance report" and "calibrate a radio".
- [Measurement method note](../METHOD.md): what is measured and how, so reports can be defended to a network operator.
- [Legal and scope note](../LEGAL.md): receive-only, what is and is not decoded, privacy and licences.

Chapters (chapters 2 to 12 follow sections 2 to 12 of Yantra's specification, chapter 13 its section 21, indoor projects):

- [1 Getting started](01-getting-started.md)
- [2 Radios](02-radios.md)
- [3 Measurement and calibration](03-measurement.md)
- [4 Decoded information](04-decoded-information.md)
- [5 Live survey](05-live-survey.md)
- [6 Recording and replay](06-recording.md)
- [7 Walk tests](07-walk-tests.md)
- [8 Cell database and network analysis](08-cell-database.md)
- [9 Terminal mode](09-terminal-mode.md)
- [10 Export and reports](10-export-and-reports.md)
- [11 Post-processing](11-post-processing.md)
- [12 Automation](12-automation.md)
- [13 Indoor projects](13-indoor-projects.md)
- [14 Plugins](14-plugins.md)
- [15 Settings and reset](15-settings.md)
- [16 Keyboard shortcuts](16-keyboard-shortcuts.md)
- [17 FAQ and troubleshooting](17-troubleshooting.md)

The pictures are made by the app itself from the demo project (the public-domain plans of the Fraunces Tavern) and the simulated radio, so they show no real network.

For maintainers: each chapter is one file; each section is a `## <number> <title>` heading, and unnumbered `###` headings divide a section further. The list of numbered sections is in `app/model/src/manual.cpp` (with its Dutch, French and German titles); `tools/ci/lint_ui.py` checks that both agree and that every "manual N.M" in the app exists.
