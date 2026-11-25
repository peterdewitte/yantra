<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Token use of the build

The tokens used by the subagent runs that built Yantra with the method of
[BUILD_PROMPT.md](BUILD_PROMPT.md), from the build's token log.

## What is counted

- One row per subagent run (planned steps, fix-up runs, retries and the
  final-report run), with the figures the agent framework reported when
  the run finished: tokens, tool uses and wall-clock time.
- The orchestrator's own conversation (planning, checking, reporting) is
  **not** included.
- 3 runs have no token count (1 in phase 12, 2 in phase 13); they are
  counted as runs but add no tokens.
- Time is wall-clock minutes per run, including waits for builds and
  tests.

## Per phase

| Phase | Runs | Tokens | Tool uses | Time (min) | Note |
|---|---:|---:|---:|---:|---|
| phase 1 | 3 | 448,539 | 170 | 26 | |
| phase 2 | 6 | 1,137,740 | 721 | 191 | |
| phase 3 | 5 | 947,513 | 497 | 126 | |
| phase 4 | 5 | 688,073 | 857 | 183 | |
| phase 5 | 6 | 1,034,831 | 1,081 | 563 | |
| phase 6 | 6 | 1,127,005 | 1,203 | 575 | |
| phase 7 | 7 | 1,300,056 | 1,133 | 356 | |
| phase 8 | 5 | 783,785 | 1,117 | 849 | |
| phase 9 | 12 | 2,610,465 | 2,689 | 2996 | |
| phase 10 | 4 | 714,608 | 828 | 724 | |
| phase 11 | 3 | 735,474 | 734 | 625 | |
| phase 12 | 8 | 1,315,126 | 1,133 | 907 | 1 run without a token count |
| phase 13 | 18 | 4,579,144 | 3,164 | 2451 | 2 runs without a token count |
| phase 14 | 11 | 1,907,566 | 1,373 | 442 | |
| phase 15 | 9 | 2,036,922 | 1,378 | 486 | |
| phase 16 | 6 | 1,211,023 | 716 | 185 | |
| final report | 2 | 435,888 | 140 | 98 | |
| release 0.3.0 | 1 | 106,978 | 72 | 68 | |
| GitHub CI and release 0.3.1 | 2 | 422,354 | 295 | 380 | |
| demo video | 2 | 539,572 | 151 | 39 | |
| **build total** | **121** | **24,082,662** | **19,452** | **12279** | 3 runs without a token count |

The phases are listed by number; they did not run in this order (the
radio parts of phases 1-5, 11 and 12 ran first, the final report came
after phase 12). The per-phase minutes are rounded, so they add up to
slightly less than the total.

## Summary

- **Total:** 121 subagent runs, 24,082,662 tokens, 19,452 tool uses,
  12,279 minutes (about 205 hours) of run time.
- **Rework** (fix-up and retry runs): 12 runs, 1,982,215 tokens, 8.2 % of
  all tokens.
- **Average per step:** 229,358 tokens over 105 steps.
- **Largest steps:**

  | Step | Tokens |
  |---|---:|
  | 13.10 | 986,548 |
  | 13.8 | 816,835 |
  | video | 539,572 |
  | 14.2 | 526,399 |
  | 11.2 | 522,588 |
