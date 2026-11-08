// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "yantra/app/app_control.h"

namespace yantra::app {

using nlohmann::json;

std::string AppControl::control(const std::string& /*plugin_id*/, const std::string& request) {
    std::string error;
    std::optional<control::Command> c;
    try {
        c = control::parse_command_json(json::parse(request), &error);
    } catch (const json::exception& e) {
        error = std::string("not a JSON request: ") + e.what();
    }
    if (!c) return control::reply_json("", control::CommandResult::failure(error)).dump();
    const std::string name = control::command_name(c->id);
    switch (c->id) {
    case control::CommandId::Status:
        return control::reply_json(name, control::CommandResult::success(status())).dump();
    case control::CommandId::Play:
    case control::CommandId::Rotate:
    case control::CommandId::Quit:
        return control::reply_json(name, control::CommandResult::failure(
                                             "not taken by the desktop app over the network (yantra-probe takes it)"))
            .dump();
    default:
        break;
    }
    {
        const std::lock_guard lock(m_);
        if (queue_.size() >= kMaxQueued)
            return control::reply_json(name, control::CommandResult::failure("too many queued commands")).dump();
        queue_.push_back(*c);
    }
    return control::reply_json(name, control::CommandResult::success(json{{"queued", true}})).dump();
}

std::vector<control::Command> AppControl::take() {
    const std::lock_guard lock(m_);
    std::vector<control::Command> out(queue_.begin(), queue_.end());
    queue_.clear();
    return out;
}

void AppControl::set_status(json status) {
    const std::lock_guard lock(m_);
    status_ = std::move(status);
}

json AppControl::status() const {
    const std::lock_guard lock(m_);
    return status_;
}

}  // namespace yantra::app
