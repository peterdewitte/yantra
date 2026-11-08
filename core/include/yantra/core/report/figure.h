// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A floor's picture for a report (EXP-05; docs/REPORT.md "Plans and their
// credits"): the floor image of the export core (the shared point list,
// IND-30) as an ImageBlock with the credit and licence of its plan sheet
// (DEMO-12). The credit comes from the sheet descriptions Yantra ships
// (data/demo/*/*.json), matched by the source's SHA-256. A
// licence-restricted sheet (a MAK sheet) is drawn only from the user's
// cached, unaltered copy, checked when the picture is made: without it,
// or when the copy differs, the picture has no plan and says why. The
// image itself is never stored by Yantra, so a report holds it only as
// rendered from that copy.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/demo/demo_project.h"
#include "yantra/core/export/image.h"
#include "yantra/core/report/document.h"

namespace yantra::report {

/// Every sheet of the demo descriptions (demo/*/*.json) in the data
/// folders, first folder first; files that are not descriptions are
/// skipped.
std::vector<demo::DemoSheet> described_sheets(const std::vector<std::filesystem::path>& data_folders);

/// The credit of a sheet description, or nullopt when it names neither a
/// licence nor a credit (e.g. the public-domain Fraunces sheets).
std::optional<PlanCredit> sheet_credit(const demo::DemoSheet& sheet);

struct FigureOptions {
    exports::ImageOptions image;  ///< font, size, banner, ...; reference_file is set here
    std::vector<demo::DemoSheet> sheets;  ///< described_sheets()
    /// The user's cache folder of downloaded restricted sheets (the MAK
    /// cache of the tour); empty: restricted plans are left out.
    std::filesystem::path sheet_cache;
};

struct Figure {
    ImageBlock block;
    bool plan_drawn = false;
    std::vector<std::string> warnings;  ///< of the image, and why a plan is left out
};

/// The picture of `table` (exports::floor_table of one floor).
/// ExportError as exports::floor_image.
Figure floor_figure(const project::Project& project, const exports::Table& table, const FigureOptions& options);

}  // namespace yantra::report
