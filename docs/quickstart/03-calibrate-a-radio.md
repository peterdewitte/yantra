<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Quick start: calibrate a radio

An SDR measures levels relative to its own full scale (dBFS). A calibration turns them into dBm at the antenna connector, per radio and per gain setting. You need the radio, a signal generator (or a calibrated reference receiver) on the network's band, a cable and attenuators. A TETRA terminal measures in dBm itself and needs no calibration.

1. **Set the radio as you will survey.** Open a project, choose the radio in Radio > Radios... and set its gains in Radio > Settings. The gain settings are part of the calibration key: a calibration made at one gain applies only to that gain.

2. **Connect the generator.** Generator output, through a known attenuation, to the radio's antenna input. The levels you type are the levels at the radio's input, so subtract the cable and attenuator loss.

3. **Plan the points.** In Radio > Calibration... type the frequencies in MHz (for example the lowest, middle and highest carrier of the network), the levels in dBm (for example -90, -70, -50), how many days the calibration stays valid and the readings per point. Choose Signal generator (or Reference radio).

4. **Measure.** Start the radio (Walk tab, Start radio), then Start in the wizard. For each point set the generator to the frequency and level shown and press Measure. A reading too close to full scale, too weak above the noise or overloaded is refused with the reason: change the level or the gain and measure again. Skip leaves a point out.

5. **Save.** Save stores one offset per frequency in your calibration table. The calibration window now shows the key as valid; the survey panels show levels in dBm without the uncalibrated flag.

6. **Verify.** Under Verify and frequency error, set the generator to another level and frequency, type them and press Verify: the error should be within a dB. Measure ppm on a known carrier gives the radio's frequency error.

7. **Reference noise.** Take the antenna away from the PC and press Take reference: the self-interference warning then compares the noise with your set-up.

8. **Keep it.** Export saves the table to a file for another PC (Import merges, the newer entry wins); a configuration profile (File > Settings > General > Save profile...) carries it too. When the calibration expires, Yantra marks the levels as expired: calibrate again.

To practise without a generator: choose the simulator, set its Levels to "dBFS (like an SDR)" (by default it reports dBm and needs no calibration), switch on "Simulated signal generator" in its settings, and run the same steps with its generator level. The entry is marked "simulated (practice)" and never applies to a real radio. Yantra never makes a calibration by itself.

Manual: chapter 3 (measurement and calibration). The method and its uncertainty: [measurement method note](../METHOD.md).
