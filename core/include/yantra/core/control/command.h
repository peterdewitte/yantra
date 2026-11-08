// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The application's control commands (API-01): one set - start/stop
// recording, monitoring on/off, play a recording, sound on/off, status,
// rotate, quit - reached the same way from the headless runner's command
// line and standard input (tools/yantra-probe), from the desktop app's
// controller and, through the plugin API's `control` service (1.1), from
// feature plugins such as the probe's REST/WebSocket API (API-02,
// PLG-24). A command has a canonical name ("record.start") and named
// string arguments; it parses from a text line ("record start survey1",
// "play file=a.yrec speed=0") or from JSON
// ({"command":"record.start","args":{"name":"survey1"}}). Every reply is
// one JSON object {"ok":true,"command":...,"result":{...}} or
// {"ok":false,"command":...,"error":"..."} (docs/API.md). Indoor only: no
// GPS commands (spec 1.2).
// Copied from yantra-v1 c24e8f5: core/engine/command.h, adapted for
// API-01 in the core (no GPS, namespace yantra::control).
#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace yantra::control {

enum class CommandId {
    RecordStart,  // args: name (optional; empty = quick name from the date/time)
    RecordStop,
    MonitorOn,    // opens the configured input
    MonitorOff,   // closes the input (recording stops)
    Play,         // args: file (recording file or folder), speed (optional, 1 = real time, 0 = fast)
    SoundOn,
    SoundOff,
    Status,
    Rotate,       // API-03: next recording segment now
    Quit,
};

struct CommandInfo {
    CommandId id;
    const char* name;                       // canonical name, e.g. "record.start"
    std::vector<const char*> args;          // accepted argument names, positional order
    size_t required = 0;                    // how many of args are required
    const char* help;                       // one line, English
};
const std::vector<CommandInfo>& command_table();
const CommandInfo& command_info(CommandId id);
const char* command_name(CommandId id);

struct Command {
    CommandId id = CommandId::Status;
    std::map<std::string, std::string> args;
    std::string arg(const std::string& key, const std::string& def = {}) const
    {
        const auto it = args.find(key);
        return it == args.end() ? def : it->second;
    }
    nlohmann::json to_json() const;   // {"command":"...","args":{...}}
    std::string to_text() const;      // "record.start name=x"
};

// "record start x", "record.start x", "record.start name=x", "quit".
// Values with spaces: name="a b" (double quotes, \" and \\ escaped).
std::optional<Command> parse_command(const std::string& line, std::string* error = nullptr);
// {"command":"record.start","args":{"name":"x"}}; argument values may be
// strings, numbers or booleans (converted to text).
std::optional<Command> parse_command_json(const nlohmann::json& j, std::string* error = nullptr);
// A JSON object when the line starts with '{', else the text form.
std::optional<Command> parse_command_any(const std::string& line, std::string* error = nullptr);

struct CommandResult {
    bool ok = true;
    std::string error;                // English, when !ok
    nlohmann::json result = nlohmann::json::object();
    static CommandResult success(nlohmann::json r = nlohmann::json::object()) { return {true, {}, std::move(r)}; }
    static CommandResult failure(std::string e) { return {false, std::move(e), nlohmann::json::object()}; }
};
nlohmann::json reply_json(const std::string& command, const CommandResult& r);

// Routes commands to handlers, one at a time (handlers run under a mutex,
// so the control socket, the start-up options and the network API can call
// execute() from any thread). A command without a handler fails with
// "not supported".
class CommandDispatcher {
public:
    using Handler = std::function<CommandResult(const Command&)>;
    void on(CommandId id, Handler h);
    CommandResult execute(const Command& c);
    // Parses a line (text or JSON), executes it and returns the reply.
    nlohmann::json execute_line(const std::string& line);

private:
    std::mutex m_;
    std::map<CommandId, Handler> handlers_;
};

}  // namespace yantra::control
