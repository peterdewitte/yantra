// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A floor's picture with its plan credit for a report (report/figure.h,
// EXP-05, DEMO-12).

#include "yantra/core/report/figure.h"

#include <algorithm>
#include <system_error>

namespace yantra::report {

namespace fs = std::filesystem;

std::vector<demo::DemoSheet> described_sheets(const std::vector<fs::path>& data_folders) {
    std::vector<demo::DemoSheet> out;
    for (const fs::path& folder : data_folders) {
        std::error_code ec;
        const fs::path dir = folder / "demo";
        if (!fs::is_directory(dir, ec)) continue;
        std::vector<fs::path> files;
        for (const auto& sub : fs::directory_iterator(dir, ec)) {
            if (!sub.is_directory()) continue;
            std::error_code ec2;
            for (const auto& e : fs::directory_iterator(sub.path(), ec2))
                if (e.path().extension() == ".json") files.push_back(e.path());
        }
        std::sort(files.begin(), files.end());  // the same order on every system
        for (const fs::path& f : files) {
            try {
                for (const demo::DemoSheet& s : demo::read_description(f).sheets) out.push_back(s);
            } catch (const std::exception&) {
                // Not a demo description (e.g. a scenario list).
            }
        }
    }
    return out;
}

std::optional<PlanCredit> sheet_credit(const demo::DemoSheet& s) {
    if (s.licence.empty() && s.credit.holder.empty()) return std::nullopt;
    PlanCredit c;
    c.title = s.title;
    std::string who = s.credit.designer;
    if (!s.credit.year.empty()) who += (who.empty() ? "" : ", ") + s.credit.year;
    if (!s.credit.inventory_number.empty()) who += (who.empty() ? "inv. " : ", inv. ") + s.credit.inventory_number;
    c.credit = s.credit.holder;
    if (!who.empty()) c.credit += (c.credit.empty() ? "" : "; ") + who;
    c.licence = s.licence;
    c.licence_url = s.licence_url;
    c.item_url = s.item_url;
    c.restricted = s.restricted;
    return c;
}

Figure floor_figure(const project::Project& project, const exports::Table& table, const FigureOptions& o) {
    Figure fig;
    exports::ImageOptions io = o.image;
    io.reference_file.clear();
    const auto floor = project.floor(table.floor_id);
    const demo::DemoSheet* sheet = nullptr;
    if (floor && !floor->source.empty())
        if (const auto src = project.source(floor->source)) {
            for (const demo::DemoSheet& s : o.sheets)
                if (s.sha256 == src->plan.sha256) {
                    sheet = &s;
                    break;
                }
            if (!src->plan.embedded && io.plan) {
                // DEMO-12: only the user's cached copy, checked now.
                std::string why;
                if (sheet == nullptr) {
                    why = "no description of this licence-restricted plan";
                } else if (o.sheet_cache.empty()) {
                    why = "no cache folder of downloaded plans";
                } else {
                    try {
                        if (const auto file = demo::check_sheet_file(*sheet, o.sheet_cache)) io.reference_file = *file;
                        else why = "it is not downloaded (" + sheet->file + ")";
                    } catch (const std::exception& e) {
                        why = e.what();
                    }
                }
                if (io.reference_file.empty())
                    fig.warnings.push_back("the licence-restricted plan is left out of the report: " + why);
            }
        }
    exports::ImageResult img = exports::floor_image(project, table, io);
    fig.plan_drawn = img.plan_drawn;
    for (auto& w : img.warnings) fig.warnings.push_back(std::move(w));
    fig.block.image = std::move(img.canvas);
    if (sheet != nullptr && fig.plan_drawn) fig.block.credit = sheet_credit(*sheet);
    return fig;
}

}  // namespace yantra::report
