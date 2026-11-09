<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# 13 Indoor projects

A project holds a building with its floors, plans, walls, antennas and walk tests.

## 13.1 Projects and floors

File > New indoor project creates a project file; File > Add floor adds a floor with its plan. The floor tabs switch between floors; floor properties set the name, level and floor-to-floor height. Changes are stored in the file as you make them.

## 13.2 Plan import and trimming

The plan import window opens a PDF or image, chooses the page, trims and rotates the part that is the floor, and sets the scale from a known distance or the drawing scale and the reference point shared by all floors.

![Two floors trimmed from one sheet](../img/plan_import_dark.png) [light theme](../img/plan_import_light.png)

## 13.3 Walls and antennas

The Edit page draws walls with their wall class and places antennas with their cell and power. Wall classes give the loss per wall at each frequency.

Propose walls looks for long straight dark lines on a floor's plan and shows them dashed and numbered. It needs a plan picture and a scale. The search runs in the background and can be cancelled. Shortest wall sets the shortest line it proposes. Accept a proposal and it becomes an ordinary wall of the class chosen under New walls, which you can edit and undo like any other. Reject it and it is forgotten. Nothing is stored in the project until you accept it, and changing floor forgets the list. Dimension lines and furniture can be proposed too, so check each one against the plan.

## 13.4 Layers

The Layers page shows or hides the plan, the interpolation, the binned squares, the walls, the antennas and the points, and sets their drawing order.

## 13.5 Interpolation

The Map page fits the propagation model to the measured points and computes a coverage map of the floor; the hover readout shows the level and how confident the map is at that spot.

![The interpolated map with walls and antennas](../img/interpolation_dark.png) [light theme](../img/interpolation_light.png)
