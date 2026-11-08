// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Control commands (API-01), see command.h.
// Copied from yantra-v1 c24e8f5: core/engine/command.cpp, adapted (no GPS).
#include "yantra/core/control/command.h"

#include <algorithm>
#include <cctype>

namespace yantra::control {

const std::vector<CommandInfo>& command_table()
{
    static const std::vector<CommandInfo> t = {
        {CommandId::RecordStart, "record.start", {"name"}, 0, "Start recording (name optional: date/time)."},
        {CommandId::RecordStop, "record.stop", {}, 0, "Stop recording; monitoring goes on."},
        {CommandId::MonitorOn, "monitor.on", {}, 0, "Open the configured radio and measure."},
        {CommandId::MonitorOff, "monitor.off", {}, 0, "Close the radio (a recording stops)."},
        {CommandId::Play, "play", {"file", "speed"}, 1, "Replay a recording file or folder (speed 1 = real time, 0 = fast)."},
        {CommandId::SoundOn, "sound.on", {}, 0, "Audible alerts on (the headless runner has no sound output: it keeps the setting)."},
        {CommandId::SoundOff, "sound.off", {}, 0, "Audible alerts off."},
        {CommandId::Status, "status", {}, 0, "Report the status (health summary)."},
        {CommandId::Rotate, "rotate", {}, 0, "Close the recording segment and start the next one."},
        {CommandId::Quit, "quit", {}, 0, "Stop recording, close the radio and exit."},
    };
    return t;
}

const CommandInfo& command_info(CommandId id)
{
    for (const auto& c : command_table())
        if (c.id == id)
            return c;
    return command_table().front();
}

const char* command_name(CommandId id) { return command_info(id).name; }

namespace {

void set_error(std::string* e, const std::string& text)
{
    if (e)
        *e = text;
}

const CommandInfo* find_command(const std::string& name)
{
    for (const auto& c : command_table())
        if (name == c.name)
            return &c;
    return nullptr;
}

bool has_arg(const CommandInfo& ci, const std::string& key)
{
    return std::any_of(ci.args.begin(), ci.args.end(), [&](const char* a) { return key == a; });
}

// Splits on spaces; double quotes group, \" and \\ escape inside quotes.
bool tokenize(const std::string& line, std::vector<std::string>& out, std::string* error)
{
    std::string cur;
    bool in_token = false, quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (quoted) {
            if (ch == '\\' && i + 1 < line.size() && (line[i + 1] == '"' || line[i + 1] == '\\')) {
                cur += line[++i];
            } else if (ch == '"') {
                quoted = false;
            } else {
                cur += ch;
            }
        } else if (ch == '"') {
            quoted = true;
            in_token = true;
        } else if (std::isspace(static_cast<unsigned char>(ch))) {
            if (in_token)
                out.push_back(cur);
            cur.clear();
            in_token = false;
        } else {
            cur += ch;
            in_token = true;
        }
    }
    if (quoted) {
        set_error(error, "unterminated quote");
        return false;
    }
    if (in_token)
        out.push_back(cur);
    return true;
}

bool check_args(const CommandInfo& ci, const Command& c, std::string* error)
{
    for (size_t k = 0; k < ci.required; ++k)
        if (c.arg(ci.args[k]).empty()) {
            set_error(error, std::string("command ") + ci.name + " needs " + ci.args[k]);
            return false;
        }
    return true;
}

}  // namespace

std::optional<Command> parse_command(const std::string& line, std::string* error)
{
    std::vector<std::string> tok;
    if (!tokenize(line, tok, error))
        return std::nullopt;
    if (tok.empty()) {
        set_error(error, "empty command");
        return std::nullopt;
    }
    std::string name = tok[0];
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return std::tolower(ch); });
    size_t next = 1;
    const CommandInfo* ci = find_command(name);
    // "record start" = "record.start".
    if (!ci && tok.size() > 1) {
        std::string sub = tok[1];
        std::transform(sub.begin(), sub.end(), sub.begin(), [](unsigned char ch) { return std::tolower(ch); });
        ci = find_command(name + "." + sub);
        next = 2;
    }
    if (!ci) {
        set_error(error, "unknown command: " + tok[0]);
        return std::nullopt;
    }
    Command c;
    c.id = ci->id;
    size_t pos = 0;
    for (size_t k = next; k < tok.size(); ++k) {
        const std::string& t = tok[k];
        const auto eq = t.find('=');
        if (eq != std::string::npos && eq > 0 && has_arg(*ci, t.substr(0, eq))) {
            c.args[t.substr(0, eq)] = t.substr(eq + 1);
            continue;
        }
        // Positional: the next argument not given by name.
        while (pos < ci->args.size() && c.args.count(ci->args[pos]))
            ++pos;
        if (pos >= ci->args.size()) {
            set_error(error, std::string("too many arguments for ") + ci->name);
            return std::nullopt;
        }
        c.args[ci->args[pos++]] = t;
    }
    if (!check_args(*ci, c, error))
        return std::nullopt;
    return c;
}

std::optional<Command> parse_command_json(const nlohmann::json& j, std::string* error)
{
    if (!j.is_object() || !j.contains("command") || !j["command"].is_string()) {
        set_error(error, "expected an object with a \"command\" string");
        return std::nullopt;
    }
    const CommandInfo* ci = find_command(j["command"].get<std::string>());
    if (!ci) {
        set_error(error, "unknown command: " + j["command"].get<std::string>());
        return std::nullopt;
    }
    Command c;
    c.id = ci->id;
    if (j.contains("args")) {
        const auto& a = j["args"];
        if (!a.is_object()) {
            set_error(error, "\"args\" must be an object");
            return std::nullopt;
        }
        for (auto it = a.begin(); it != a.end(); ++it) {
            if (!has_arg(*ci, it.key())) {
                set_error(error, "unknown argument " + it.key() + " for " + ci->name);
                return std::nullopt;
            }
            const auto& v = it.value();
            if (v.is_string())
                c.args[it.key()] = v.get<std::string>();
            else if (v.is_number() || v.is_boolean())
                c.args[it.key()] = v.dump();
            else {
                set_error(error, "argument " + it.key() + " must be a string, number or boolean");
                return std::nullopt;
            }
        }
    }
    if (!check_args(*ci, c, error))
        return std::nullopt;
    return c;
}

std::optional<Command> parse_command_any(const std::string& line, std::string* error)
{
    const auto first = line.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && line[first] == '{') {
        const nlohmann::json j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded()) {
            set_error(error, "invalid JSON");
            return std::nullopt;
        }
        return parse_command_json(j, error);
    }
    return parse_command(line, error);
}

nlohmann::json Command::to_json() const
{
    nlohmann::json a = nlohmann::json::object();
    for (const auto& [k, v] : args)
        a[k] = v;
    return {{"command", command_name(id)}, {"args", a}};
}

std::string Command::to_text() const
{
    std::string s = command_name(id);
    for (const auto& [k, v] : args) {
        s += ' ' + k + '=';
        const bool quote = v.empty() || v.find_first_of(" \t\"\\") != std::string::npos;
        if (!quote) {
            s += v;
            continue;
        }
        s += '"';
        for (char ch : v) {
            if (ch == '"' || ch == '\\')
                s += '\\';
            s += ch;
        }
        s += '"';
    }
    return s;
}

nlohmann::json reply_json(const std::string& command, const CommandResult& r)
{
    nlohmann::json j = {{"ok", r.ok}, {"command", command}};
    if (r.ok)
        j["result"] = r.result;
    else
        j["error"] = r.error;
    return j;
}

void CommandDispatcher::on(CommandId id, Handler h)
{
    std::lock_guard<std::mutex> lk(m_);
    handlers_[id] = std::move(h);
}

CommandResult CommandDispatcher::execute(const Command& c)
{
    std::lock_guard<std::mutex> lk(m_);
    const auto it = handlers_.find(c.id);
    if (it == handlers_.end() || !it->second)
        return CommandResult::failure(std::string(command_name(c.id)) + " is not supported");
    try {
        return it->second(c);
    } catch (const std::exception& e) {
        return CommandResult::failure(e.what());
    }
}

nlohmann::json CommandDispatcher::execute_line(const std::string& line)
{
    std::string err;
    const auto c = parse_command_any(line, &err);
    if (!c)
        return reply_json("", CommandResult::failure(err));
    return reply_json(command_name(c->id), execute(*c));
}

}  // namespace yantra::control
