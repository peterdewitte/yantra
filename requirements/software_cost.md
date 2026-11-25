<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Software cost estimate (COCOMO II)

What it would cost to build Yantra the conventional way, by a team of
developers, estimated with the COCOMO II model. Code as of commit 09ded24
(version 0.3.1 plus this folder), 2026-11-25.

## Summary

| | Effort | Schedule | Team | Cost at $10,000/PM | Cost at $15,000/PM |
|---|---:|---:|---:|---:|---:|
| **Whole product** (105.6 KSLOC) | **494 person-months** | 26.4 months | 19 | **$4.94 M** | $7.41 M |
| Radio parts (39.8 KSLOC) | 169 PM | 18.8 months | 9 | $1.69 M | $2.53 M |
| Application, GUI + core (65.8 KSLOC) | 293 PM | 22.4 months | 13 | $2.93 M | $4.40 M |
| Whole product with tests (164.6 KSLOC) | 806 PM | 30.8 months | 26 | $8.06 M | $12.09 M |

PM = person-month. All COCOMO settings are Nominal; see "Sensitivity".

## Model

COCOMO II.2000 post-architecture model, as in the calculator at
<https://softwarecost.org/tools/COCOMO/> (that page shows no formulas; with
every rating at Nominal it reports the size exponent 1.0997, the value used
here):

- Effort: PM = A × Size^E × EAF, with A = 2.94, Size in KSLOC and
  E = B + 0.01 × ΣSF = 0.91 + 0.01 × 18.97 = **1.0997** (scale factors at
  Nominal: PREC 3.72, FLEX 3.04, RESL 4.24, TEAM 3.29, PMAT 4.68).
- Effort adjustment factor: EAF = product of the 17 effort multipliers =
  **1.00** (all Nominal).
- Schedule: TDEV = C × PM^F, with C = 3.67 and
  F = D + 0.2 × (E − B) = 0.28 + 0.2 × 0.1897 = **0.318**.
- Team = PM / TDEV; cost = PM × cost per person-month.

## Size

Physical source lines: non-blank lines that are not comments, counted from
the files under git.

- Counted: C and C++ (sources and headers), Rust, Python, CMake, shell,
  PowerShell, NSIS.
- Not counted: `third_party/` (Dear ImGui, miniaudio, osmo-tetra, libairspy
  and the other bundled libraries), the vendored library sources under
  `tools/deps/`, documentation, data, images and generated files.
- Tests: everything under `tests/`, `test_*` files and fuzzers, shown
  apart. COCOMO normally sizes the delivered product, so the main figure
  leaves the tests out.

| Part | Product code | Tests |
|---|---:|---:|
| Radio parts | 37,113 | 24,863 |
| Application (GUI + core) | 61,313 | 33,881 |
| Shared (build, packaging, CI, doc tools) | 7,129 | 141 |
| **Total** | **105,555** | **58,885** |

By language (product code): C/C++ 94.3k, Python 5.3k, Rust 3.0k, CMake
2.6k, shell and NSIS 0.7k.

### What belongs to which part

- **Radio parts**: `radio-api/`, `radio-common/` (receiver, TETRA decoder,
  SoapySDR and TETRA common code), the radio plugins (`plugins/hackrf`,
  `soapy`, `pei`, `simulator`, `example`), `gen/` (test-signal
  generator), the radio tools (`tools/yantra-radio-host`, `yantra-hackrf`,
  `yantra-gen`, `yantra-pei-sim`, `yantra-sens`), the SDK and dependency
  scripts (`tools/sdk`, `tools/deps`), `udev/`, and `tests/radio*`.
- **Application**: `app/` (the GUI), `core/`, `plugin-api/`, the Yantra
  plugins (`plugins/tetra`, `probe`, `beacon`), `tools/yantra-plugin-test`,
  `data/` and the other tests.
- **Shared**: `cmake/`, `packaging/`, `tools/ci`, `tools/docs`,
  `tools/screenshots` and the top-level CMake files, divided between the
  two parts in proportion to their size (37 % radio, 63 % application) for
  the per-part figures.

The two parts together (462 PM) come to less than the whole product
(494 PM): COCOMO grows faster than linearly with size, so the model sees the
cost of integrating the parts only in the whole-product figure.

## Sensitivity

All ratings are Nominal, which understates some of Yantra's properties:

- The radio parts do signal processing, TETRA decoding and real-time work
  on SDR hardware. With Product Complexity (CPLX) at Very High (×1.34) the
  radio parts come to about **226 PM, $2.3-3.4 M**.
- Required reliability, two platforms (Linux and Windows) and platform
  volatility would raise the estimate further; an experienced, cohesive
  team with good tools would lower it.
- The cost per person-month is an input, not part of the model: $10,000
  and $15,000 are shown; the cost scales linearly with it.

## How it was actually built

Yantra was built with Claude (Anthropic's AI assistant) by the method in
[BUILD_PROMPT.md](BUILD_PROMPT.md), from 2026-11-03 to 2026-11-25, in 121
subagent runs using about 24.1 million tokens
([TOKENS.md](TOKENS.md)). That figure leaves out the orchestrating
conversation and the owner's own time for the specifications, decisions
and testing.

The COCOMO figures estimate a conventional team project. They are not a
measure of what this build cost.
