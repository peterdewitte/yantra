<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# 6 Recording and replay

Recordings keep every measurement record of a survey with its time, its calibration and its flags, so it can be replayed and analysed later exactly as it was measured. They are files of their own in the `recordings` folder next to the project (format: `docs/formats/RECORDING.md`), listed in the project when they stop.

## 6.1 Recording

The recording controls are in the Walk tab of the project window:

- Name and notes of the recording; split (start a new file every so many minutes, 0 for one file) and zstd (compress the files).
- Record starts a recording with these settings; Quick record starts one at once with a name from the time; Stop recording ends it.
- Marker adds a marker with the label you typed at this moment.
- New file closes the file and goes on in a new one; New file + pause closes it and waits until you press Resume.
- Voice note records a short spoken note through the microphone, stored next to the recording; press it again to stop.

A walk test always records: it records into the running recording, or starts one named after the walk and stops it at the end. While a recording runs, Exit, Close project and closing the window are refused, so nothing is lost by accident; stop the recording first.

### Ring buffer

With Ring buffer on, Yantra keeps the last minutes (2 to 60) of measurements while no recording runs, as files of one minute in your cache folder. When something happens that you want to keep, type a name and press Save ring buffer: the kept minutes become a recording of the project. Whether the ring buffer is on when a project opens is set in File > Settings > Survey (15.2).

## 6.2 Replay

Open Replay in the Walk tab, choose a recording of the project and press Open; a running radio stops first. The replay plays the recording through the same processing as a live survey, so the panels, graphs, events and statistics show what was measured then. Play, Pause, Stop and Again control it; the speed field sets how fast (1 = real time, 0 = as fast as possible); the position slider and a click in a graph move to any moment; a marker in the list jumps to it. Voice notes are played at their moment. Close replay ends it.

Cut: start to cursor and Cut: cursor to end save that part of the recording as a new recording with the name you typed, for example to remove the walk to the building.

To place a recording's measurements on a floor afterwards, load it in the Map tab's Advanced dropdown (11.1).

## 6.3 Markers and voice notes

Markers note a moment of the survey with a label (a room, a door, a problem); they are stored in the recording, listed in the replay and exported with the points. Voice notes record a short spoken note with the microphone; they are WAV files next to the recording and play at their moment in the replay.
