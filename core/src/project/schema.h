// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Project file schema and its migrations (docs/formats/PROJECT.md;
// internal to the core). Copied from yantra-v2 68ea20a:
// core/src/project/schema.h, adapted for IND-01.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "sqlite.h"

namespace yantra::project::schema {

// Brings the schema from version `from` (0 = empty file) to `to`, one
// step per version, each in its own transaction that also sets
// PRAGMA user_version. Versions above the built-in ones take their SQL
// from `extra` (tests). `now` and `tool` fill the rows a step adds;
// `fault` is called before each step's commit (tests).
void migrate(sql::Db& db, int from, int to, const std::string& now, const std::string& tool,
             const std::vector<std::string>& extra = {},
             const std::function<void(std::string_view)>& fault = {});

}  // namespace yantra::project::schema
