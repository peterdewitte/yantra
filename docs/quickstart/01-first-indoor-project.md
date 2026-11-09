<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Quick start: first indoor project

Import a plan, trim the floors, walk, report. About 15 minutes. You need a floor plan (PDF or image) with a scale bar or a known distance, and a calibrated radio (see [Calibrate a radio](03-calibrate-a-radio.md)), or the simulator "Demo (simulated network)" to practise. Help > Tour does the same steps on an example building.

1. **New project.** Start Yantra and choose New indoor project. Type the project name, the site, the network operator and where to save the file, then OK. Changes are stored as you make them.

2. **Import the plan.** File > Add floor > Choose a plan file... and open the sheet. The plan import window opens (choose the page of a PDF).

3. **Trim the floors.** With Rectangle (or Polygon for an irregular outline) drag around the first floor; Deskew straightens a tilted plan. Press Add as floor..., type the floor's name and level, and Add. Do the same for every other floor on the sheet: one sheet can give several floors.

4. **Scale and reference.** Select a floor, choose Scale bar and drag along the plan's scale bar, type its length in metres and Apply (or Scale line over a known distance). Floors of the same sheet can share the scale. With Reference click a spot found on every floor, such as a staircase corner. Close the window.

5. **Walls and antennas.** On the Edit page choose the wall class under New walls and draw the main walls with Draw wall (W): a click per corner, Enter to end. Propose walls can suggest walls from the plan; accept only the real ones. Place the DAS antennas with Place antenna (A) and set their cell and power in the properties.

6. **Walk.** On the Walk page choose the radio and the kind Point and click, then Start. Click where you stand, walk straight at a steady pace and click where you are at every turn. Stop at the end. The points appear coloured by level. With the simulator choose Mouse survey instead and move the mouse slowly over the plan.

7. **Coverage map.** On the Map page press Run. The map is fitted to your points and fades out where nobody measured. Rest the pointer on the plan to see measured and interpolated values.

8. **Next floor.** Choose the next floor's tab and repeat steps 6 and 7.

9. **Report.** File > Report: type your name as the surveyor, keep PDF and HTML, and press Make report. The report is written into the `reports` folder next to the project.

Next: [Indoor walk test with acceptance report](02-walk-test-acceptance.md). Manual: chapters 13 (projects, plans, walls, interpolation), 7 (walk tests) and 10 (reports).
