// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-11 S: the wall editor's proposals (contract in wall_proposals.h).

#include "yantra/app/wall_proposals.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>
#include <utility>

#include "yantra/app/floor_editor.h"
#include "yantra/app/i18n.h"
#include "yantra/app/project_window.h"
#include "yantra/core/project/project.h"

namespace yantra::app {

namespace {

// A proposal this close to a wall of the floor, along its whole length, is
// that wall: a second run does not offer what was accepted or drawn already.
constexpr double kSameWallM = 0.3;

double to_segment(plan::Point2 p, plan::Point2 a, plan::Point2 b) {
    const double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    const double t = len2 > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0) : 0.0;
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

bool along_existing(const plan::ProposedSegment& s, const std::vector<building::Wall>& walls, double tol_units) {
    constexpr int kSamples = 5;
    for (int i = 0; i < kSamples; ++i) {
        const double t = static_cast<double>(i) / (kSamples - 1);
        const plan::Point2 p{s.a.x + (s.b.x - s.a.x) * t, s.a.y + (s.b.y - s.a.y) * t};
        bool near = false;
        for (const building::Wall& w : walls) {
            for (std::size_t k = 0; !near && k + 1 < w.points.size(); ++k)
                near = to_segment(p, w.points[k], w.points[k + 1]) <= tol_units;
            if (near) break;
        }
        if (!near) return false;
    }
    return true;
}

}  // namespace

struct WallProposals::Run {
    std::vector<plan::ProposedSegment> found;  // written by the job before it ends
};

WallProposals::WallProposals(project::Project& project, ProjectWindow& window, FloorEditor& editor)
    : project_(project), window_(window), editor_(editor) {}

WallProposals::~WallProposals() = default;

std::string WallProposals::start_reason() const {
    if (job_) return tr("Walls are being proposed.");
    const std::optional<building::Floor> f = project_.floor(window_.selected());
    if (!f) return tr("Select a floor first.");
    if (f->source.empty() || f->trim.shape == building::Trim::Shape::None) return tr("This floor has no plan picture to look at.");
    if (!project_.building().has_scale(*f)) return tr("This floor has no scale yet: the lengths of walls need it.");
    return {};
}

void WallProposals::forget() {
    job_.reset();  // cancels and waits
    run_.reset();
    proposals_.clear();
    note_.clear();
    floor_.clear();
}

Result WallProposals::start() {
    const std::string why = start_reason();
    if (!why.empty()) return Result{false, why};
    forget();
    const std::string id = window_.selected();
    const building::Floor f = *project_.floor(id);
    const double mpu = *project_.building().metres_per_unit(f);
    std::shared_ptr<const plan::PlanSource> source;
    try {
        source = window_.plan_source(id);
    } catch (const std::exception& e) {
        return Result{false, std::string(tr("The plan cannot be shown: ")) + e.what()};
    }
    floor_ = id;
    metres_per_unit_ = mpu;
    run_ = std::make_shared<Run>();
    job_ = util::Job::start([run = run_, source = std::move(source), f, mpu, options = options_](util::JobContext& ctx) {
        plan::WallDetectControl control;
        control.cancelled = [&ctx] { return ctx.cancelled(); };
        control.progress = [&ctx](double p) { ctx.progress(p); };
        std::vector<plan::ProposedSegment> found = plan::propose_walls(*source, f.page, f.trim, mpu, options, control);
        if (ctx.commit()) run->found = std::move(found);
    });
    return Result{true, tr("Looking for walls on the plan in the background...")};
}

void WallProposals::cancel() {
    if (job_) job_->cancel();
}

void WallProposals::wait() const {
    if (job_) job_->wait();
}

void WallProposals::finish() {
    const util::JobStatus s = job_->status();
    if (s.state == util::JobState::Failed) {
        note_ = std::string(tr("No walls were proposed: ")) + s.error;
    } else if (s.state == util::JobState::Cancelled) {
        note_ = tr("Stopped: no walls were proposed.");
    } else {
        const std::vector<building::Wall> walls = project_.walls(floor_);
        const double tol = metres_per_unit_ > 0.0 ? kSameWallM / metres_per_unit_ : 0.0;
        for (const plan::ProposedSegment& p : run_->found) {
            if (along_existing(p, walls, tol)) continue;
            proposals_.push_back(WallProposal{next_id_++, p.a, p.b, p.length_m});
        }
        note_ = proposals_.empty() ? std::string(tr("No new walls were found on the plan."))
                                   : std::string(tr("Proposed walls: accept the right ones; they are not in the project until then."));
    }
    job_.reset();
    run_.reset();
}

void WallProposals::tick() {
    if (floor_.empty()) return;
    if (window_.selected() != floor_) {
        forget();
        return;
    }
    if (job_ && job_->finished()) finish();
}

WallProposalsView WallProposals::view() const {
    WallProposalsView v;
    v.note = note_;
    v.proposals = proposals_;
    if (job_) {
        v.running = true;
        v.progress = job_->status().progress;
        v.note.clear();
    }
    return v;
}

void WallProposals::set_min_length_m(double metres) {
    if (std::isfinite(metres)) options_.min_length_m = std::clamp(metres, 0.2, 100.0);
}

Result WallProposals::accept(int id) {
    const auto it = std::find_if(proposals_.begin(), proposals_.end(), [id](const WallProposal& p) { return p.id == id; });
    if (it == proposals_.end()) return Result{false, tr("That proposal is no longer there.")};
    const Result r = editor_.add_proposed_walls({{it->a, it->b}});
    if (r.ok) proposals_.erase(it);
    return r;
}

Result WallProposals::accept_all() {
    if (proposals_.empty()) return Result{false, tr("There are no proposed walls.")};
    std::vector<std::pair<plan::Point2, plan::Point2>> segments;
    segments.reserve(proposals_.size());
    for (const WallProposal& p : proposals_) segments.emplace_back(p.a, p.b);
    const Result r = editor_.add_proposed_walls(segments);
    if (r.ok) proposals_.clear();
    return r;
}

void WallProposals::reject(int id) {
    std::erase_if(proposals_, [id](const WallProposal& p) { return p.id == id; });
}

void WallProposals::reject_all() {
    proposals_.clear();
    note_.clear();
}

}  // namespace yantra::app
