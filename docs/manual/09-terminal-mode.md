<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# 9 Terminal mode

A terminal plugin reads the cell information of a connected radio terminal and shows it like a radio. Yantra ships one: the TETRA terminal plugin (`pei`), which reads a TETRA terminal in trunked mode over its Peripheral Equipment Interface (PEI, AT commands). It only reads: it sends no calls, messages or other traffic, and the terminal's levels are already in dBm, so no level calibration is needed.

## 9.1 Connecting a terminal

Connect the terminal's PEI cable (USB or serial adapter) and choose Radio > Radios..., then Find radios. Every serial port that may hold a terminal is listed under the TETRA terminal plugin; a port you may not open is listed with the reason (on Linux your user needs the `dialout` group). Connect starts reading: the terminal's serving cell and the neighbour cells it reports appear in the survey panels and the walk test like the cells of any other radio (chapter 5 and 7). Unplugging the cable stops the device; plugging it in again and Find radios brings it back.

## 9.2 Terminal windows

The terminal's windows are the generated radio windows of chapter 2, filled from what the plugin declares:

- Settings: the serial port and baud rate, the command profile that says how the terminal reports its serving and neighbour cells, the frequency band and carrier offset of the network, and, under Advanced settings, the commands themselves, the reply timeout and the command log. Settings marked as needing a restart apply after you confirm a restart.
- Debug: the latest cell records with all their fields, the commands that timed out or are not supported by the terminal, and the device log (with the command log on, every command and reply).
- Device info and self-test: manufacturer, model, firmware and serial number (TEI), registration and location area, and a self-test that checks the port, the AT replies, the radio info, the level, the registration and both cell commands, with what to do when an item fails.

The serving and neighbour cell commands differ between terminal makers. The default profile speaks a documented example format; for other terminals enter their commands under Advanced settings (see the terminal's PEI manual).
