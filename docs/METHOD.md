<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Measurement method

What Yantra measures, how, and how far each figure can be trusted (DOC-04), so a coverage or acceptance report can be explained to a network operator. It describes Yantra as built today, with the TETRA radio plugins of yantra-radios. The developer documents named in each section hold the exact rules and their tests.

## 1 Chain of measurement

1. A **radio plugin** (yantra-radios) receives the TETRA downlink, measures every carrier it hears and decodes the broadcast information of the carriers it can synchronise to. It sends Yantra **measurement records** only: no samples, no messages. Records arrive at the rate set in the radio's settings (by default one per carrier per second).
2. Yantra's **measurement pipeline** turns the levels into dBm with the calibration, checks each record (refuses impossible levels, ignores out-of-order and duplicate records, flags late ones) and closes one **measurement cycle** per period: every second (five per second in the simulator's mouse survey). A cycle has one sample per carrier: its newest record, the age of that record and the flags ([MEASUREMENT_PIPELINE.md](MEASUREMENT_PIPELINE.md)).
3. A **walk test** places the cycles on the floor plan; each cycle is at most one **measured point** ([SURVEY.md](SURVEY.md)).
4. **Statistics, maps and reports** read the stored points from one shared list, so the project window, the exports and the report always count the same points.

## 2 What is measured

### Level (RSSI)

The level is the power of the whole TETRA carrier over the record's interval. The radio plugin moves the channel to the carrier's frequency, filters it with a root-raised-cosine filter matched to the TETRA pulse (roll-off 0.35) and divides the mean power by the share of a TETRA carrier the filter passes, so a carrier a few kHz off its channel is still measured in full, while a carrier 25 kHz away is suppressed by about 70 dB. On generated test signals the level is linear within +/-0.5 dB from -5 to -100 dBFS. Details: yantra-radios `docs/MEASUREMENT.md`.

An SDR measures in **dBFS** (relative to its own full scale). Yantra converts to **dBm at the antenna connector** with the calibration (section 3):

    level_dBm = level_dBFS + offset_dB(frequency)

No antenna gain or cable correction is applied in this version: the figures refer to the radio's input. A TETRA terminal measures in dBm itself.

Carriers the radio hears but cannot synchronise to are reported with their level only (flag `no_sync`), so they appear in the scanner but are not a serving cell.

### C1

C1 is the path-loss criterion of EN 300 392-2 (clause 23):

    C1 = level - RXLEV_ACCESS_MIN - max(0, MS_TXPWR_MAX_CELL - MS power)

with the two access parameters as the cell broadcasts them and the MS power of the radio's setting "MS maximum power (C1)" (30 dBm, a hand-portable, by default). A C1 delivered by the radio is used as it is; otherwise Yantra calculates it from the calibrated level and marks it `c1_estimated`. An uncalibrated level gets no C1.

### Quality

On a synchronised carrier the decoder gives, over its last quality window: the message erasure rate, CRC error rates per logical channel, the bit error rate, MER and EVM, the SNR and the frequency error. Quality values are shown and reported as they are; they are not calibrated.

### Identity and broadcast parameters

Cell identity (MCC, MNC, LA, colour code, carrier), the broadcast parameters and the neighbour list come from the cell's broadcast messages (SYNC, SYSINFO, D-NWRK-BROADCAST). They are compared with the cell database for the verification tables. A cell is identified by its carrier and MCC/MNC/LA.

### Flags

Every value carries the flags of its sample: `no_sync`, `overload` (clipping), `uncalibrated`, `simulated`, `held` (no new record this cycle), `stale`, `late`, `device_lost`, `data_loss` and `c1_estimated`. Flagged values are kept and shown with their flags rather than dropped, so a report never hides a problem.

## 3 Calibration and its uncertainty

A calibration entry belongs to one **calibration key**: the radio's driver and serial number plus every setting that changes its gain. It holds one offset per frequency, measured with a signal generator (or against a calibrated reference receiver) at several levels and averaged; the spread over the levels is kept. Between calibration frequencies the offset is interpolated linearly in dB; up to 5 MHz beyond them the nearest offset is used and the value marked *extrapolated*; further out the level stays uncalibrated. An entry past its validity is still used but marked *expired*. The entry used is stored with every recording, so a survey can be checked later against its calibration ([CALIBRATION.md](CALIBRATION.md)).

Sources of uncertainty, per radio type:

- **Every SDR**: the generator's level accuracy and the loss of the cable and attenuators used for the calibration add directly to the result; the receiver's own linearity (+/-0.5 dB on test signals); the spread of the offset over the calibration levels; temperature and ageing between calibration and survey (hence the validity days).
- **HackRF One and RTL-SDR**: 8-bit samples give a small dynamic range at one gain setting. Near a strong site the radio clips (flag `overload`; the Clipping sensor); far from it weak carriers sink into the noise. Choose the gain for the building and calibrate at that gain.
- **Airspy and other SoapySDR radios**: more bits and so more dynamic range, the same calibration rules.
- **TETRA terminal**: the terminal reports its own levels in dBm; Yantra applies no calibration to them. Their accuracy is the terminal maker's, typically coarser than a calibrated SDR; compare once with a calibrated SDR before mixing their results.
- **Simulator**: its levels come from a simulated network. A practice calibration (marked "simulated (practice)") applies only to the simulator, never to a real radio; the simulator's mouse survey makes one by itself when your table has none, holds it only while the survey runs and never writes it to your table.

**Hardware status.** None of the radios has been measured with Yantra on real hardware yet. The figures above come from tests on generated signals and simulated devices; the measured hardware results will be listed in DEVICES.md of the yantra-radios project. Until a radio is listed there, verify it against a known signal before a survey (calibration window, Verify).

**Self-interference.** A laptop and its USB cables radiate noise that raises the noise floor near the antenna. Yantra warns when the noise under the decoded carriers is 6 dB or more above the reference (the thermal floor of a 25 kHz channel with a 15 dB noise figure, about -115 dBm, or a reference you measured with the antenna away from the PC), or when three or more carriers are heard without sync, for three cycles in a row. Keep the antenna away from the PC; the warning is part of the survey panels and the status bar ([MEASUREMENT_PIPELINE.md](MEASUREMENT_PIPELINE.md), "Self-interference").

## 4 Placing the measurements

- **Point and click / continuous walk**: the surveyor clicks a position at every turn; the cycles between two clicks are spread evenly along the straight line between them by time. The position error therefore depends on walking at a steady pace and clicking on time; it is not measured by Yantra.
- **Manual points**: the cycles of N seconds at one clicked spot become one point; levels, C1 and other dB values are averaged as power (10 log10 of the mean of 10^(x/10)), which estimates the local mean power under fading; other units arithmetically.
- **Mouse survey (simulator only)**: the simulator measures at the mouse position; at most one point per 0.25 m.
- At about 1 m/s and one record per second, each point's level is a mean over about a metre of walk, more than a wavelength at 400 MHz (0.75 m), which smooths fast fading.

## 5 Statistics and coverage percentages

- **Which points count**: every stored point of the selection (no thinning or re-sampling). A point without a value for the metric, or with a value in another unit (an uncalibrated dBFS point in a dBm list), is counted but is in no share: for coverage it counts as **not covered**.
- **Coverage share** at a threshold = points with a value at or above the threshold, in percent of **all** points.
- **Percentiles**: linear interpolation between closest ranks (as the spreadsheet function PERCENTILE.INC). Means are arithmetic in dB; the power mean is given separately.
- **Binning** (tested area): the points of a floor are grouped into squares (2 m by default) and each square gets a percentile of its points (the median by default). Squares never cross floors; a floor without a scale has no squares. Binning is an extra, labelled view; it never replaces the points.
- **Serving-cell changes**: counted in walking order; a serving cell is its identity and its carrier.

### Acceptance profiles and their basis

An acceptance profile is a set of criteria "at least X % at or above Y"; it passes when every criterion passes. Its **basis** says what X % is of:

- **Measured points**: all points of the selection (above).
- **Tested area**: the binned squares whose percentile reaches Y, in percent of the squares walked.
- **Floor area (interpolated)**: the cells of the interpolated map inside the floor's outline (its trim) whose confidence is at least the profile's minimum (0.5 by default), counting those whose interpolated value reaches Y; over several floors, the floors are weighted by their assessed area. Without a stored map the floor is "not assessed".

A project without stored profiles is reported with the default profile "Indoor acceptance": 95 % of the measured points at or above -95 dBm. The report gives every profile on the measured points and on the floor area, plus the tested area when that is the profile's basis; the profile's own basis decides PASS or FAIL. The share of measured points and the share of floor area are always shown side by side, never combined into one figure.

## 6 The interpolated coverage map

The map predicts the level between and beyond the measured points from the building model ([PROPAGATION.md](PROPAGATION.md)):

1. **Ray tracing per floor.** From every antenna, rays go to every cell of a grid over the floor (0.25 m by default): the direct ray and specular reflections on the drawn walls up to the set order (2 by default, at most 3). Each ray loses free-space path loss up to a reference distance and a log-distance law beyond it, the loss of every wall crossed (per wall class and frequency), a loss per reflection, the antenna pattern, and for antennas on other floors the slab loss per floor. The rays of the antennas of one cell add in power.
2. **Fit.** The model's parameters (EIRP per antenna group, loss per wall class, loss per floor) are fitted to the measured points by least squares within bounds. Parameters no point depends on keep their defaults and are listed. The fit quality (rms of model minus measurement) is reported.
3. **Estimated sources.** When no antenna of the building reaches the floor, sources are estimated from the points; they are drawn and labelled "estimated sources" and are never written into the building.
4. **Residuals.** The remaining difference at each point (measured minus model) is spread with inverse-distance weighting over 10 m, only between places that see each other without crossing a wall; at a measured point the map equals the measurement, far from the points it falls back to the fitted model.
5. **Confidence mask.** Each cell's confidence is 1 within 2 m of a measured point it sees, 0 beyond 10 m, linear between, and 0 outside the floor's outline. The map's colours fade with the confidence, and the floor-area basis counts only confident cells.

The map is always labelled "interpolated" and kept apart from the measured points; it is marked stale when the walls, antennas or points change after it was made.

**Limits of the model.** 2D floor geometry with straight vertical walls; no diffraction around wall ends or door frames, no scattering, no ceiling or floor reflections; powers add without phase, so the map is a local mean without fast fading or standing waves; the reflection loss does not depend on the angle; outdoor donor directions are not traced. The fit is local: with few points, the EIRP of nearby antennas and the path-loss exponent can trade against each other while the fitted map stays close. The residual spreading does not model the correlation of shadowing. A wall class's default losses are estimates from published ranges (COST 231, ITU-R P.1238 and P.2040) until the fit adjusts them. A floor without a scale cannot be traced. Use the map to see where the measured points do not reach and to show the trend between them; acceptance on the floor-area basis is only as good as the walls drawn and the points walked.

## 7 What the report states

The report names the radios (driver, serial number, plugin), their calibration status (valid, expired, uncalibrated or "simulated (practice)"), the cell database, the plugins, the walk tests per floor, the threshold and every profile with its basis. Simulated data carries the DEMO label. Numbers in the report are the same numbers the project window shows, from the same point list.
