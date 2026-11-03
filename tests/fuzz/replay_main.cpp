// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Driver of a fuzz target outside libFuzzer (RAD-96): runs every file of
// the given files and folders through LLVMFuzzerTestOneInput, then, with
// --mutations N, N seeded mutations of each (bit flips, byte changes,
// truncation, duplication, insertion; xorshift seed --seed, default 1), so
// a normal or sanitizer build checks more than the corpus itself. Folders
// that do not exist are skipped; at least one input must run. A crash or
// abort is the failure, exactly as under libFuzzer. With the environment
// variable YANTRA_FUZZ_SAVE=<file> every input is written there before it
// runs, so after a crash that file holds the input that crashed.
//   fuzz_<target> [--mutations N] [--seed S] <file|folder>...
// Copied from yantra 68ea20a: tests/fuzz/replay_main.cpp.

#include "fuzz.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Rng {
    uint64_t s;
    uint64_t next()
    {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 0x2545F4914F6CDD1DULL;
    }
    size_t below(size_t n) { return n ? static_cast<size_t>(next() % n) : 0; }
};

std::vector<uint8_t> read_file(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void mutate(std::vector<uint8_t>& d, Rng& r)
{
    const int edits = 1 + static_cast<int>(r.below(4));
    for (int e = 0; e < edits; ++e) {
        switch (r.below(6)) {
        case 0:
            if (!d.empty())
                d[r.below(d.size())] ^= static_cast<uint8_t>(1u << r.below(8));
            break;
        case 1:
            if (!d.empty())
                d[r.below(d.size())] = static_cast<uint8_t>(r.next());
            break;
        case 2:
            d.resize(r.below(d.size() + 1));
            break;
        case 3:
            d.insert(d.begin() + static_cast<std::ptrdiff_t>(r.below(d.size() + 1)), static_cast<uint8_t>(r.next()));
            break;
        case 4:
            if (!d.empty()) {
                const size_t a = r.below(d.size()), n = 1 + r.below(std::min<size_t>(64, d.size() - a));
                std::vector<uint8_t> chunk(d.begin() + static_cast<std::ptrdiff_t>(a),
                                           d.begin() + static_cast<std::ptrdiff_t>(a + n));
                d.insert(d.begin() + static_cast<std::ptrdiff_t>(r.below(d.size() + 1)), chunk.begin(), chunk.end());
            }
            break;
        default:
            if (!d.empty()) {
                // A few interesting byte values.
                static const uint8_t kVals[] = {0x00, 0xFF, 0x7F, 0x80, '"', '{', '[', '0', '-', 'e'};
                d[r.below(d.size())] = kVals[r.below(sizeof kVals)];
            }
            break;
        }
    }
}

}  // namespace

int main(int argc, char** argv)
{
    long mutations = 0;
    uint64_t seed = 1;
    std::vector<fs::path> files;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--mutations" && i + 1 < argc) {
            mutations = std::stol(argv[++i]);
        } else if (a == "--seed" && i + 1 < argc) {
            seed = std::stoull(argv[++i]);
        } else if (fs::is_directory(a)) {
            for (const auto& e : fs::recursive_directory_iterator(a))
                if (e.is_regular_file())
                    files.push_back(e.path());
        } else if (fs::is_regular_file(a)) {
            files.emplace_back(a);
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::cerr << "no inputs (corpus folders missing?)\n";
        return 1;
    }
    const char* save = std::getenv("YANTRA_FUZZ_SAVE");
    auto run = [save](const std::vector<uint8_t>& d) {
        if (save != nullptr && *save != '\0') {
            std::ofstream out(save, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
        }
        LLVMFuzzerTestOneInput(d.data(), d.size());
    };
    Rng rng{seed ? seed : 1};
    uint64_t runs = 0;
    for (const fs::path& f : files) {
        const std::vector<uint8_t> d = read_file(f);
        run(d);
        ++runs;
        for (long m = 0; m < mutations; ++m) {
            std::vector<uint8_t> x = d;
            mutate(x, rng);
            run(x);
            ++runs;
        }
    }
    std::cout << files.size() << " inputs, " << runs << " runs\n";
    return 0;
}
