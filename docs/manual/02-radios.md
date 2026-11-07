<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# 2 Radios

Radios are plugins (chapter 14). Yantra lists the devices every radio plugin finds and generates the windows of each radio from what the plugin declares.

## 2.1 The radio list

Radio > Radios... lists every device found, with its plugin, state and serial number. Connect starts a device, Disconnect stops it; Find radios searches again, for example after plugging a radio in.

## 2.2 Radio settings

The settings window of a device shows the plugin's settings with their units and ranges, the value the device reports back next to the one you asked for, presets and Reset to defaults. Settings marked as needing a restart apply after you confirm a restart of the stream.

## 2.3 The debug window

The debug window shows the latest measurement records with all their fields and flags, the record rate per carrier, the stream counters, events, the sensors and the device log. It never shows raw samples.

## 2.4 Device info and self-test

The device info window shows the device entry, module and firmware versions and the sensors, and runs the plugin's self-test with a pass or fail per item and what to do when an item fails.
