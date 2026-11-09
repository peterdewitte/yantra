<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# 7 Walk tests

A walk test ties the measurements to positions on the floor plan.

## 7.1 Walking a floor

Start a walk on the Walk page, click your position on the plan at every turn and walk at a steady pace; Yantra places the measurements along the line between the clicks.

![A walk test on the first floor, with the legend](../img/walk_test_dark.png) [light theme](../img/walk_test_light.png)

With the simulator as the radio, the Mouse survey kind lets the mouse stand in for you: move it slowly over the plan and each point is stored where the simulator measured; a click on the plan runs the interpolation; Esc stops. When your calibration table has no entry for the simulator, the survey uses a simulated practice calibration, so its levels are in dBm. The Walk page and the report say so; it is not added to your table, and a real radio never gets one.

## 7.2 Manual points

A manual point measures for a few seconds at one spot you click on the plan.
