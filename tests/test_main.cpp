// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared doctest main of every test binary (Yantra TST-01). It also takes
// the option --update (golden files, RAD-93; see golden.hpp), which it
// removes before doctest reads the command line.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "golden.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
bool g_update = false;
}

namespace yantra::tests {

bool update_golden()
{
    const char* env = std::getenv("YANTRA_UPDATE_GOLDEN");
    return g_update || (env != nullptr && *env != '\0' && std::strcmp(env, "0") != 0);
}

}  // namespace yantra::tests

int main(int argc, char** argv)
{
    std::vector<char*> args;
    for (int i = 0; i < argc; ++i) {
        if (i > 0 && std::strcmp(argv[i], "--update") == 0)
            g_update = true;
        else
            args.push_back(argv[i]);
    }
    doctest::Context ctx;
    ctx.applyCommandLine(static_cast<int>(args.size()), args.data());
    return ctx.run();
}
