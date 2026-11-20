<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# 14 Plugins

Technologies, radios and extra features are plugins, so Yantra itself knows no radio and no network technology. The plugins shipped with Yantra:

- Technology: TETRA (`tetra`): the names, units, help texts and translations of the TETRA fields, the channel raster, C1, the reselection rule, the verification rules and the calculator (chapters 4 and 8).
- Radios, from the radio parts: the simulator, HackRF, the SoapySDR receiver and the TETRA terminal (chapters 2 and 9).
- Feature: the probe plugin (`probe`): the headless probe's remote API and the Remote probes window (chapter 12).

An example plugin ("beacon") and the plugin test kit are for plugin writers; how to write a plugin is in `docs/PLUGINS.md`.

## 14.1 The plugin manager

File > Settings > Plugins lists every plugin with its name, version, API version, author, licence, status and message. A plugin switched off stays installed and keeps its settings; switching a plugin on or off is refused while a walk test, a recording, the radio or a replay runs, and makes Yantra find the radios again. With the probe plugin (chapter 12) or the TETRA terminal plugin (chapter 9) switched off or not installed, every other part of Yantra works as before, and without the probe plugin Yantra has no network listener. Without the TETRA technology plugin the radios' records are still shown, with their plain field names.

## 14.2 Plugin windows

Plugins can add windows and entries to the File, View, Radio, Tools and Help menus (the View menu appears only when a plugin adds entries; the Tools menu is always there, with Spectrum first), for example the TETRA calculator and Remote probes under Tools. Their windows follow the app's theme, touch layout, language and help rules: every control has a tooltip, a greyed control says why, and a control that changes something important asks first. A plugin switched off loses its windows.
