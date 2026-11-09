// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Walls proposed from the plan image in the wall editor (IND-11, priority
// S): core plan::propose_walls() in the background, the result as a list of
// not-yet-accepted proposals. Proposals live here, in memory, and never reach
// the project: an accepted one becomes an ordinary wall of the class chosen
// for new walls, through FloorEditor (one undo step), and is then edited like
// any other; a rejected one is forgotten. Changing floor forgets the rest.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "yantra/app/result.h"
#include "yantra/core/plan/wall_detect.h"
#include "yantra/core/util/job.h"

namespace yantra::project {
class Project;
}

namespace yantra::app {

class FloorEditor;
class ProjectWindow;

struct WallProposal {
    int id = 0;  ///< stable while the list lives; shown as its number
    plan::Point2 a;
    plan::Point2 b;
    double length_m = 0.0;
};

struct WallProposalsView {
    bool running = false;
    double progress = 0.0;
    std::string note;  ///< the outcome of the last run ("" none)
    std::vector<WallProposal> proposals;
};

class WallProposals {
public:
    WallProposals(project::Project& project, ProjectWindow& window, FloorEditor& editor);
    ~WallProposals();

    /// Why a run cannot start on the selected floor ("" it can): no plan
    /// picture, no scale, or a run already going.
    std::string start_reason() const;
    /// Starts in the background. The list so far is dropped.
    Result start();
    /// Stops a run; nothing is proposed.
    void cancel();
    /// Blocks until a run has ended (tests); the next tick() picks it up.
    void wait() const;
    /// Each frame: picks up a finished run; a floor change forgets the list.
    void tick();

    WallProposalsView view() const;

    /// The shortest wall proposed (metres, 0.2 .. 100): the next run uses it.
    double min_length_m() const noexcept { return options_.min_length_m; }
    void set_min_length_m(double metres);

    /// Becomes a wall of the editor's class for new walls (one undo step).
    Result accept(int id);
    Result accept_all();
    /// Forgotten (the project never had it).
    void reject(int id);
    void reject_all();

private:
    struct Run;
    void forget();
    void finish();

    project::Project& project_;
    ProjectWindow& window_;
    FloorEditor& editor_;
    plan::WallProposalOptions options_;
    std::string floor_;  ///< the floor the list or the run is of
    double metres_per_unit_ = 0.0;
    std::shared_ptr<Run> run_;
    std::unique_ptr<util::Job> job_;
    std::vector<WallProposal> proposals_;
    std::string note_;
    int next_id_ = 1;
};

}  // namespace yantra::app
