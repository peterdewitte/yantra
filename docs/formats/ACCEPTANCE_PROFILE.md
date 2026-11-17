<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Acceptance profile (`yantra-acceptance-profile`, version 1)

An acceptance profile (EXP-06) is the set of criteria "X % of the points
(or of the area) at or above Y" that a report checks a floor against. The
profiles are stored in the project file, one row per profile in the
table `acceptance_profiles` (project schema 7, [PROJECT.md](PROJECT.md)):
the row holds the profile's name and position, the column `definition`
holds the JSON object described here. There is no separate profile file;
profiles travel with their project, so every computer that opens the
project evaluates the same criteria.

Code: `core/include/yantra/core/survey/acceptance.h`
(`profile_json`, `profile_from_json`, `acceptance_profiles`,
`set_acceptance_profiles`), `core/src/survey/acceptance.cpp`. How a
profile is evaluated: [SURVEY.md](../SURVEY.md) "Acceptance profiles
(stored)" and [INTERPOLATION.md](../INTERPOLATION.md) "Readout and statistics".

## Members

| Member | Type | Meaning | Default when absent |
| --- | --- | --- | --- |
| `format` | text | `"yantra-acceptance-profile"` | required |
| `version` | integer | `1` | required |
| `unit` | text | unit of the thresholds (`dBm` for levels); a point whose value has another unit, or none, counts as not covered | `"dBm"` |
| `basis` | text | what the percentage counts: `points` (measured points), `tested_area` (binned squares of the walked area) or `floor_area` (cells of a stored interpolated map inside the floor outline) | `"points"` |
| `criteria` | list, at least one | each `{"threshold": number, "min_percent": number}`: at least `min_percent` % (0..100) at or above `threshold`; every criterion must pass | required |
| `square_m` | number 0.1..100 | `tested_area`: square size in metres | `2` |
| `square_percentile` | number 0..100 | `tested_area`: the value of a square is this percentile of its points (50 = median) | `50` |
| `min_confidence` | number 0..1 | `floor_area`: least confidence of a map cell that counts as assessed (IND-43) | `0.5` |

The name is not part of the JSON: it is the row's `name` (not empty,
unique in the project). Writing (`profile_json`) always writes every
member.

## Version handling

- Another `format`, a missing or non-integer `version`, or a missing
  `criteria` list is refused.
- A `version` above 1 is refused: "the profile was saved by a newer
  Yantra (version N)".
- Unknown members are ignored, so version 1 can gain optional members
  without breaking older readers.
- A value out of range (an empty name or unit, no criteria, a threshold
  that is not finite, a percentage outside 0..100, a square outside
  0.1..100 m, a confidence outside 0..1) is refused with the first
  problem. A project whose stored profile cannot be read gives a
  "damaged" project error naming the profile.

## Example

The definition of a profile that asks 95 % of the floor's tested squares
at -95 dBm or better and 99 % at -105 dBm or better:

<!-- example: yantra-acceptance-profile -->
```json
{
  "format": "yantra-acceptance-profile",
  "version": 1,
  "unit": "dBm",
  "basis": "tested_area",
  "criteria": [
    {"threshold": -95, "min_percent": 95},
    {"threshold": -105, "min_percent": 99}
  ],
  "square_m": 2,
  "square_percentile": 50,
  "min_confidence": 0.5
}
```

Where no profile is stored the report uses the default profile "Indoor
acceptance": 95 % of the points at or above -95 dBm, basis `points`, and
marks it as the default.
