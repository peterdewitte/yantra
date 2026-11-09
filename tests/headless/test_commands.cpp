// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The control commands (API-01): text and JSON forms, replies, dispatcher.
#include <doctest/doctest.h>

#include <yantra/core/control/command.h>

using namespace yantra::control;

TEST_CASE("API-01 commands parse from text lines") {
    std::string e;
    auto c = parse_command("record start survey1", &e);
    REQUIRE_MESSAGE(c, e);
    CHECK(c->id == CommandId::RecordStart);
    CHECK(c->arg("name") == "survey1");
    c = parse_command("record.start name=\"a b\"", &e);
    REQUIRE_MESSAGE(c, e);
    CHECK(c->arg("name") == "a b");
    c = parse_command("play file=a_0000.yrec speed=0", &e);
    REQUIRE_MESSAGE(c, e);
    CHECK(c->id == CommandId::Play);
    CHECK(c->arg("file") == "a_0000.yrec");
    CHECK(c->arg("speed") == "0");
    for (const auto& [line, id] : std::vector<std::pair<std::string, CommandId>>{
             {"record stop", CommandId::RecordStop}, {"monitor on", CommandId::MonitorOn},
             {"monitor off", CommandId::MonitorOff}, {"sound on", CommandId::SoundOn},
             {"sound off", CommandId::SoundOff}, {"status", CommandId::Status},
             {"rotate", CommandId::Rotate}, {"quit", CommandId::Quit}}) {
        CAPTURE(line);
        c = parse_command(line, &e);
        REQUIRE_MESSAGE(c, e);
        CHECK(c->id == id);
        // The text form round-trips.
        const auto again = parse_command(c->to_text(), &e);
        REQUIRE(again);
        CHECK(again->id == id);
    }
    CHECK_FALSE(parse_command("play", &e));  // file is required
    CHECK_FALSE(e.empty());
    CHECK_FALSE(parse_command("gps on", &e));  // indoor only: no GPS (spec 1.2)
    CHECK_FALSE(parse_command("record start name=x bogus=1", &e));
    // Values with any space character the tokenizer splits on (\v, \f,
    // \r as well as blanks), quotes or backslashes are quoted by
    // to_text, so they come back unchanged (found by fuzz_command).
    for (const std::string& v : {std::string("a\vb"), std::string("a\fb"), std::string("a\rb"), std::string("a b"),
                                std::string("q\"x"), std::string("back\\slash"), std::string("nul\0in", 6)}) {
        Command w;
        w.id = CommandId::RecordStart;
        w.args["name"] = v;
        const auto back = parse_command(w.to_text(), &e);
        REQUIRE_MESSAGE(back, e);
        CHECK(back->args == w.args);
    }
}

TEST_CASE("API-01 commands parse from JSON and reply in one form") {
    std::string e;
    auto c = parse_command_json(nlohmann::json::parse(R"({"command":"play","args":{"file":"x.yrec","speed":2}})"), &e);
    REQUIRE_MESSAGE(c, e);
    CHECK(c->arg("speed") == "2");
    CHECK(parse_command_json(c->to_json(), &e)->arg("file") == "x.yrec");
    CHECK_FALSE(parse_command_json(nlohmann::json::parse(R"({"args":{}})"), &e));
    CHECK_FALSE(parse_command_json(nlohmann::json::parse(R"({"command":"nope"})"), &e));
    c = parse_command_any(R"({"command":"status"})", &e);
    REQUIRE(c);
    CHECK(c->id == CommandId::Status);

    const nlohmann::json ok = reply_json("status", CommandResult::success({{"state", "stopped"}}));
    CHECK(ok["ok"] == true);
    CHECK(ok["command"] == "status");
    CHECK(ok["result"]["state"] == "stopped");
    const nlohmann::json bad = reply_json("rotate", CommandResult::failure("no recording runs"));
    CHECK(bad["ok"] == false);
    CHECK(bad["error"] == "no recording runs");
}

TEST_CASE("API-01 the dispatcher runs handlers and refuses commands without one") {
    CommandDispatcher d;
    int runs = 0;
    d.on(CommandId::Status, [&](const Command&) {
        ++runs;
        return CommandResult::success();
    });
    CHECK(d.execute_line("status")["ok"] == true);
    CHECK(runs == 1);
    const nlohmann::json r = d.execute_line("quit");
    CHECK(r["ok"] == false);
    CHECK(d.execute_line("not a command")["ok"] == false);
    CHECK(command_table().size() == 10);
}
