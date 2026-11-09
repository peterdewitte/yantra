<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# 17 FAQ and troubleshooting

Answers to common questions and problems. A greyed control always says why it is greyed: rest the pointer on it first.

### The radio is not found or cannot be opened

- Radio > Radios... lists a device that cannot be opened with the reason. Missing USB permissions: install the udev rules (1, "Radio drivers and permissions") and plug the radio in again.
- An RTL-SDR that is busy is usually held by the DVB-T television driver: install the blacklist file and plug it in again.
- A TETRA terminal's port needs your user in the `dialout` group.
- The Radio menu shows a plugin's submenu only when that plugin found a device. Press Find radios after plugging a radio in.
- A plugin switched off in File > Settings > Plugins finds nothing (14.1).

### The levels are in dBFS or marked uncalibrated

The radio has no calibration for its current calibration key: its driver, serial number and gain settings. A gain change needs its own calibration. Run the calibration wizard (3.1) at the gains you survey with. Interpolation and C1 need calibrated levels.

### The levels look wrong

- A self-interference warning: move the antenna away from the PC and its cables (3.1).
- The overload flag or the Clipping sensor of an SDR: lower the gain (for the HackRF the preset "Near site, strong signal").
- The calibration is expired or extrapolated (more than its frequencies cover): calibrate again.
- None of the radios has been measured with Yantra on real hardware yet (2, "Hardware status"): compare with a reference radio.

### "Dropped" in the status bar

The PC did not keep up with the radio. Close other programs, lower the radio's sample rate or its records per second, and keep the project on a fast disk.

### The interpolation cannot run

Rest the pointer on Run: it needs a floor with a scale (13.2), measured points with levels in dBm, and no other run going on. A map marked stale was made before the last change of walls, antennas or points: Run again.

### The mouse survey asks me to move slower

The mouse moved faster than the speed warning (2 m/s on the plan's scale by default, 7.1). Move it slowly, as a person would walk.

### A project does not open

- The project is locked: it is open in another Yantra (or another window). Close it there.
- A project made by a newer Yantra is refused; update Yantra.
- A PDF plan is refused with "built without PDF support" when Yantra was built without PDFium: use an image of the plan, or a build with PDF support.

### The tour cannot download a MAK plan

The download needs a network and your consent, and the file must match the expected SHA-256; otherwise the tour says why and continues with the Fraunces Tavern (1.3). Nothing is kept in the cache when the download fails.

### Yantra does not start or shows a black window

Try the other window system: `yantra --window-system x11` (or `wayland`), and set it in File > Settings > General (15.1). Under WSL, X11 is preferred.

### I want to start over

Help > Reset all settings (15.4) sets every setting back to its default after a backup; projects, recordings and cell databases are kept. `yantra --reset-settings` does the same without a window.

### Does Yantra send anything over the network?

Only when you ask: the sharing of exports and reports (10.4) when you choose a method, the MAK plan download of the tour after you agree (1.3), the probe plugin's API once you start it (chapter 12), and a radio plugin's GSMTAP output when you switch it on (chapter 2). See the [legal and scope note](../LEGAL.md).
