// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The third-party and data credits of the About dialog (UI-15, NFR-01):
// read from THIRD_PARTY.md (installed as share/yantra/THIRD_PARTY.md; in
// a build tree the sources' copy next to data/), so the dialog and the
// file never disagree. The "Libraries" table gives the third-party
// credits, the "Data" table the data credits (Library of Congress, MAK,
// logos).
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace yantra::app {

struct CreditRow {
    std::string name;     ///< component or data item
    std::string licence;  ///< SPDX expression or licence text
    std::string source;   ///< where it comes from (URL or credit)
};

struct Credits {
    std::vector<CreditRow> libraries;
    std::vector<CreditRow> data;
    std::filesystem::path file;  ///< the THIRD_PARTY.md read ("" none)
    std::string error;           ///< why there are no credits ("" when read)
};

/// The rows of the "## Libraries" and "## Data" tables of a THIRD_PARTY.md
/// text (columns by header: Component/Item, Licence, Source), Markdown
/// code marks removed.
Credits parse_credits(const std::string& markdown);

/// `<folder>/THIRD_PARTY.md`, else `<folder>/../THIRD_PARTY.md` (the
/// sources' data/ folder), of the first data folder that has one.
std::optional<std::filesystem::path> find_third_party(const std::vector<std::filesystem::path>& data_folders);

/// find_third_party + parse_credits; `error` says when it is missing.
Credits load_credits(const std::vector<std::filesystem::path>& data_folders);

}  // namespace yantra::app
