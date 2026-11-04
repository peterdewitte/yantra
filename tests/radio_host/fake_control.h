// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test control of the fake radio plugin (fake_radio.cpp): the plugin
// exports fake_control(); the radio host tests look it up in the same
// loaded module and drive the plugin from the test thread (pushes,
// stream events, unplug/replug, hangs, bad result codes, context
// queries). Only for the tests; no part of the radio API.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <yantra/radio.h>

namespace fake {

// Calls that can be made to hang or to return a given code.
enum Call : int { kOpen = 0, kStart, kStop, kEnumerate, kSetSetting, kCallCount };

struct ContextAnswer {
    yr_status plan = YR_OK, cursor = YR_OK, walk = YR_OK, scene = YR_OK, scene_small = YR_OK;
    bool functions_set = false;  // the host table had the four context functions
    yr_floor_plan_info plan_info{};
    yr_plan_position cursor_pos{};
    yr_plan_position walk_pos{};
    std::string scene_json;
    std::uint32_t scene_length = 0;
};

struct Control {
    std::mutex m;
    std::condition_variable cv;
    // Behaviour.
    bool present = true;
    bool hang[kCallCount] = {};
    yr_status result[kCallCount] = {};  // returned instead of the real result when != 0
    bool released = false;              // ends every hang
    bool block_context = false;         // on_context_event waits until cleared
    // Observations.
    int hanging = 0;  // calls inside a hang now
    int context_waiting = 0;  // on_context_event calls inside now
    int inits = 0, shutdowns = 0, opens = 0, closes = 0, starts = 0, stops = 0;
    std::map<std::string, std::string> settings;  // applied setting -> value text
    std::vector<std::string> setting_order;
    std::map<std::uint32_t, int> context_events;
    std::uint64_t last_plan_hz = 0;
};

using ControlFn = Control* (*)();
using PushFn = int (*)(int count, int invalid);  // returns the YR_OK results
using PushOneFn = int (*)(std::int64_t monotonic_ns, std::uint64_t hz, double level,
                          std::uint32_t flags);  // returns the yr_status of push_record
using EventFn = void (*)(std::uint32_t event, std::int64_t value);
using UnplugFn = void (*)();
using ReplugFn = void (*)(int send_back);  // 1: DEVICE_BACK on the stream, 0: devices_changed
using QueryFn = void (*)(ContextAnswer* out);

}  // namespace fake
