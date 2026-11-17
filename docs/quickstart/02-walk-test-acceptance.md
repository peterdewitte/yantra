<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Quick start: indoor walk test with acceptance report

Walk a building against agreed acceptance criteria and hand over a PASS or FAIL report. You need a project with its floors and scales (see [First indoor project](01-first-indoor-project.md)), the cell database of the network, and a radio calibrated at the gains you will use (see [Calibrate a radio](03-calibrate-a-radio.md)).

1. **Cell database.** Network > Cell database...: import the operator's cell list as CSV (or type the cells) and Apply. Received cells are then named, and the report checks them.

2. **Acceptance profile.** File > Report > Edit profiles: add a profile with the agreed criteria, for example "95 % at or above -95 dBm", and choose its basis: the measured points, the tested area (walked squares of 2 m) or the floor area of the interpolated map (with the least confidence counted). Save. Without a profile the report uses the default "Indoor acceptance", 95 % of the measured points at or above -95 dBm.

3. **Before you walk.** Connect the radio and check it in the Radio menu: no self-interference warning, no overload, levels in dBm (not dBFS). Keep the antenna away from the laptop; a USB extension cable helps. Start a recording if you want the way to the building too (Survey page).

4. **Walk every floor.** On the Walk page choose Point and click or Continuous and Start. Click at every turn, walk at a steady pace through every room and corridor the acceptance covers, and use Manual points for rooms you cannot walk through. Stop at the end of each floor, then walk the next floor's tab. Every walk test is recorded.

5. **Check the coverage.** On the Post page look at Good/bad against the threshold and at the Acceptance figures per floor; walk again where the plan shows gaps. On the Map page press Run per floor for the floor-area figures.

6. **Verify the network.** Network > Verification... (live or on a replay) lists cells that differ from the database, new and missing cells, and the neighbour cross-check. They are also in the report.

7. **Report.** File > Report: type your name as the surveyor, choose the floors and the profiles, PDF and HTML, and Make report. The report gives per floor the plan with every point, the coverage map, the statistics, the share of measured points next to the share of floor area (never mixed), and each profile's PASS or FAIL; the network verification follows.

8. **Hand it over.** Optionally tick "Send the files when written" with a shared folder, an upload address or your own mail server (Sharing settings). Nothing is sent unless you choose a method.

Manual: chapters 7 (walk tests), 8 (cell database), 10 (reports and acceptance profiles) and 11 (post-processing). How the figures are computed: [measurement method note](../METHOD.md).
