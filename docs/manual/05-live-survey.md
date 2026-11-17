<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# 5 Live survey

The Walk tab of the project window shows the live values of what the radio measures now, or the moment of a replay (6.2): the serving cell, its level, quality and C1, the sample count and a small level graph. The survey windows below (Neighbours, Graphs, Scanner, Events, Statistics) open from the Window menu. Each item of the Window menu is a switch: choose it to open the window, choose it again to close it; the menu ticks the windows that are open.

### Watching without a walk

The Walk tab starts the radio without a walk test: it names the radio in use (or "No radio") and its state (idle, measuring, walk test); Select... opens Radio > Radios... (2.1), which searches for devices the first time and chooses one and shows what the search found per plugin, Start radio measures and fills the live values and the windows, Stop radio stops. A walk test (chapter 7) starts the radio by itself. After Stop radio or the end of a walk the radio stays connected, so its windows (chapter 2) keep their device.

## 5.1 Survey panels

The Neighbours window (Window > Neighbours) shows the large values of the serving cell (level, quality, C1), its identity, channel and frequency, its broadcast parameters, the cell name from the cell database (chapter 8), the floor and the sample counter. The neighbour list shows, in different colours, the neighbours that are announced and received, received only, and announced only. Rest the pointer on a value for its name, unit and source.

![The survey panels: serving cell, large values and neighbours](../img/survey_dark.png) [light theme](../img/survey_light.png)

## 5.2 Graphs

The Graphs window (Window > Graphs) draws the level, C1 and quality of the serving cell and up to 31 neighbours over the last minutes (the graph span, 15.2), with optional threshold lines from the cell's broadcast parameters. Cells get the colour of the cell database, else one of twelve colours. Clicking or dragging in a graph sets a cursor: every window then shows that moment; Live follows the newest values again. In a replay the cursor moves the replay.

![The level and C1 graphs](../img/survey_graphs_dark.png) [light theme](../img/survey_graphs_light.png)

## 5.3 Scanner, events and statistics

The Scanner window lists every carrier the radio sees, by frequency, also the ones it cannot decode. Its State column shows the sample flags (for example "no_sync" or "late"); a list too long for the column ends in "..." and shows in full when you hover over it. The Events window shows serving-cell changes, sync losses, device and recording events and data losses, newest first. The Statistics window counts serving-cell changes and sync losses, the time in and out of coverage and the share of samples at or above the coverage threshold (the threshold field in the Statistics window; its default is in File > Settings > Survey, 15.2).

![The scanner](../img/survey_scanner_dark.png) [light theme](../img/survey_scanner_light.png)
![The event list](../img/survey_events_dark.png) [light theme](../img/survey_events_light.png)

The self-interference warning (3.1, "Reference noise and self-interference") shows as a line in the Walk tab's live values.

## 5.4 The status bar

The status bar shows the last message, the radio's state (or the replay), the recording state, a "Dropped" count in amber when the radio dropped records, the self-interference warning, the free disk space where the project is, and the battery or mains state. Warnings are amber. Dropped records mean the PC did not keep up with the radio: close other programs, lower the radio's sample rate or its records per second.

## 5.5 Sounds and level alerts

Yantra can sound the serving level as a tone at an interval (a higher tone for a stronger level, from -120 to -40 dBm), and give a sound on loss of sync, on a new serving cell and on a low battery. Level alerts watch up to 8 cells (by name, identity or carrier in MHz): when a cell drops below its threshold or is no longer received, a sound and, if you want, a popup tell you, once, until the level has been 2 dB above the threshold again. A sound never cuts off a playing voice note. A replay plays no sounds. Set them up in File > Settings > Sound (15.3).
