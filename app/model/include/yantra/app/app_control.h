// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The desktop app's side of the plugin API's control service (API-01/02,
// PLG-24): the probe plugin's remote API runs the app's commands through
// it. A plugin calls control on its own thread (an HTTP worker), so this
// never waits for the UI thread. "status" is answered from the copy the UI
// thread last stored; other commands are queued, answered {"queued":true}
// and run by AppState::tick() on the UI thread. Commands of the headless
// runner the desktop app does not take from the network (play, rotate,
// quit) are refused at once.
#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "yantra/core/control/command.h"
#include "yantra/core/plugin/loader.h"

namespace yantra::app {

class AppControl final : public plugin::Controller {
public:
    static constexpr std::size_t kMaxQueued = 32;

    std::string control(const std::string& plugin_id, const std::string& request) override;

    /// UI thread: the queued commands, oldest first (the queue is emptied).
    std::vector<control::Command> take();
    /// UI thread: the status reply's result for the next "status" requests.
    void set_status(nlohmann::json status);
    nlohmann::json status() const;

private:
    mutable std::mutex m_;
    std::deque<control::Command> queue_;
    nlohmann::json status_ = nlohmann::json::object();
};

}  // namespace yantra::app
