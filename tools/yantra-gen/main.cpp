// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-gen: the TETRA test-signal generator command (RAD-71). See
// gen_app.hpp and docs/YANTRA_GEN.md.
#include "gen_app.hpp"
#include "yantra/gen/portable_math.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    yantra::gen::use_portable_math();
    std::ios::sync_with_stdio(false);
    std::vector<std::string> args(argv + 1, argv + argc);
    return yantra::gentool::run(args, std::cout, std::cerr, argc > 0 ? argv[0] : "");
}
