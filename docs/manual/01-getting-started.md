<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# 1 Getting started

Yantra measures TETRA and other radio networks inside buildings: you import floor plans, walk the floors with a radio and get coverage maps and reports.

## 1.1 The start screen

The start screen offers New indoor project, Open project, the demo project, the recent projects and the guided tour. Every button explains itself when you rest the pointer on it; a greyed button says why it cannot be used yet.

![The start screen with the File menu](../img/start_menu_dark.png) [light theme](../img/start_menu_light.png)

## 1.2 About Yantra

Help > About (also File > About) shows the version, the build date, the licence (GPL-3.0) with the link to the source code, the third-party and data credits and the loaded plugins with their versions and licences.

![The About dialog](../img/about_dark.png) [light theme](../img/about_light.png)

## 1.3 The guided tour

Help > Tour (also offered on the start screen the first time, and `yantra --tour`) walks through a whole indoor survey on an example building, in demo mode: the DEMO label is shown throughout and the radio is the simulator "Demo (simulated network)", so no radio is needed.

The first step chooses the building. The Fraunces Tavern comes with Yantra. The three MAK buildings (Beer house, Haus Claeson, Villa Bunzl) use a plan sheet of the MAK collection in Vienna that Yantra does not ship: the tour asks first, and only after you agree downloads the sheet once into your cache folder (`~/.cache/yantra/mak`, or the folder in `YANTRA_MAK_CACHE`). Only the plain request is sent, with the program name and version. The file is checked against its SHA-256, size and pixel size and kept unchanged; the project refers to it. The credit, the licence and the note that the scale is approximate are shown with the plan. If you say no, there is no network, or the file is not the expected one (the message shows both SHA-256 values and the address), the tour continues with the Fraunces Tavern.

The steps: a new indoor project, the plan sheet, several floors trimmed from it, a floor from a second plan (the Fraunces Tavern only), the scale, walls, DAS antennas, the mouse survey (move the mouse slowly over the plan; a click starts the interpolation; without a calibration of your own the simulator's levels get a simulated practice calibration, which the Walk page and the report state), the points and the coverage map, switching floors, the report (a later version) and the finished example project.

Each step frames the control it is about in orange and says what to do. Do it yourself, or press Show me: it does the step with the example's data through the same commands. Back repeats a step, Skip goes on, Leave closes the tour; Help > Tour opens it again where you left it. Reset all settings (15.4) starts the tour over.
