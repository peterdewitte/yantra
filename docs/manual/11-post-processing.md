<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# 11 Post-processing

The Post page of the side panel shows the measured points of the shown floor in different views. The plan, the binned squares, the legend and the figures on the page all follow the view, and so do File > Export and File > Report (chapter 10). Measured serving level puts the plain view back.

## 11.1 Views of the points

- Load recordings: choose a folder and load a recording into the project as walk-test points, placed as they were walked. A recording already in the project is refused, so no point is counted twice.
- Carrier: the serving cell, the strongest cell, or one carrier you type in MHz.
- Value: the level, C1 or a quality field of that carrier (for example MER or BER).
- View: Values colours the points by the legend; Good/bad colours them against the threshold; Offset adds the dB you type to every level, to see what a stronger or weaker system would give (the plan then shows the label "Simulation: ...").
- Serving cell changes marks every point where the serving cell changed with a yellow square.
- Failed cells: switch cells off to see the best remaining server at every point, chosen again from the measured levels, for example to check what a site failure would leave.
- Bin percentile: the percentile of each binned square (the median by default); the square size is set on the Layers page (13.4).

The figures give the points' statistics, the share at or above the threshold and the coverage of the tested area. Rest the pointer on a point for its position, its serving and strongest cell and every neighbour the cell announced, with the levels measured.

## 11.2 Acceptance criteria

Acceptance on the Post page evaluates one criterion, such as 95 % at or above -95 dBm, on the basis you choose and shows PASS or FAIL:

- Points: the share of all measured points of the floor. A point without a value, or with an uncalibrated level, counts as not covered.
- Tested area: the share of the walked squares (binned, 13.4) whose percentile reaches the level.
- Floor area: the share of the floor's interpolated map with enough confidence (13.5) that reaches the level.

Stored acceptance profiles, with several criteria each, belong to the report (10.3).
