// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-sens: decoder sensitivity sweep (RAD-126). See docs/SENSITIVITY.md.
// Copied from yantra 68ea20a: tools/yantra-sens/main.cpp, adapted (the
// baseline check per scenario, its verdicts as CSV).

#include "yantra/sens/sweep.hpp"

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace sens = yantra::sens;

namespace {

constexpr const char* kBegin = "<!-- BEGIN GENERATED: yantra-sens -->";
constexpr const char* kEnd = "<!-- END GENERATED: yantra-sens -->";

void usage(std::ostream& os)
{
    os << "usage: yantra-sens [options]\n"
          "  --tier quick|full     SNR grid, runs and duration (default quick)\n"
          "  --scenario NAME       only this scenario (repeatable); --list shows them\n"
          "  --snr A,B,...         SNRs in dB instead of the tier's\n"
          "  --runs N, --seconds S runs per point and signal per run instead of the tier's\n"
          "  --seed N              base seed (default 1)\n"
          "  --threads N           worker threads (default: all cores)\n"
          "  --isa simd|scalar     DSP kernels (default simd)\n"
          "  --csv FILE            write the results as CSV\n"
          "  --markdown FILE       write the Markdown tables\n"
          "  --update-doc FILE     replace the generated part of a Markdown file\n"
          "  --baseline FILE       only check the expectations of FILE (of the --scenario ones\n"
          "                        when given; --csv writes the verdicts); exit 1 on a failure\n";
}

std::vector<double> parse_list(const std::string& s)
{
    std::vector<double> v;
    std::stringstream ss(s);
    for (std::string x; std::getline(ss, x, ',');)
        v.push_back(std::stod(x));
    return v;
}

int update_doc(const std::string& path, const std::string& tables)
{
    std::ifstream in(path);
    std::stringstream buf;
    buf << in.rdbuf();
    std::string text = buf.str();
    const auto b = text.find(kBegin);
    const auto e = text.find(kEnd);
    if (!in || b == std::string::npos || e == std::string::npos || e < b) {
        std::cerr << "yantra-sens: " << path << " has no generated section (" << kBegin << " ... " << kEnd << ")\n";
        return 2;
    }
    text = text.substr(0, b + std::string(kBegin).size()) + "\n\n" + tables + "\n" + text.substr(e);
    std::ofstream(path) << text;
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        std::string tier_name = "quick";
        std::vector<std::string> only;
        std::vector<double> snr;
        int runs = 0;
        double seconds = 0.0;
        uint64_t seed = 1;
        unsigned threads = 0;
        auto isa = yantra::receiver::Isa::Simd;
        std::string csv, markdown, doc, baseline;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            const auto value = [&]() -> std::string {
                if (i + 1 >= argc)
                    throw std::invalid_argument(a + " needs a value");
                return argv[++i];
            };
            if (a == "--help" || a == "-h") {
                usage(std::cout);
                return 0;
            } else if (a == "--list") {
                for (const sens::Scenario& s : sens::scenarios())
                    std::cout << s.name << "\t" << s.description << "\n";
                return 0;
            } else if (a == "--tier") {
                tier_name = value();
            } else if (a == "--scenario") {
                only.push_back(value());
            } else if (a == "--snr") {
                snr = parse_list(value());
            } else if (a == "--runs") {
                runs = std::stoi(value());
            } else if (a == "--seconds") {
                seconds = std::stod(value());
            } else if (a == "--seed") {
                seed = std::stoull(value());
            } else if (a == "--threads") {
                threads = static_cast<unsigned>(std::stoul(value()));
            } else if (a == "--isa") {
                const std::string v = value();
                if (v != "simd" && v != "scalar")
                    throw std::invalid_argument("--isa simd|scalar");
                isa = v == "scalar" ? yantra::receiver::Isa::Scalar : yantra::receiver::Isa::Simd;
            } else if (a == "--csv") {
                csv = value();
            } else if (a == "--markdown") {
                markdown = value();
            } else if (a == "--update-doc") {
                doc = value();
            } else if (a == "--baseline") {
                baseline = value();
            } else {
                usage(std::cerr);
                return 2;
            }
        }
        sens::Tier t = sens::tier(tier_name);
        if (!snr.empty())
            t.snr_db = snr;
        if (runs > 0)
            t.runs = runs;
        if (seconds > 0.0)
            t.seconds = seconds;

        if (!baseline.empty()) {
            std::ifstream in(baseline);
            if (!in)
                throw std::runtime_error("cannot read " + baseline);
            std::vector<sens::Expectation> expect;
            for (const sens::Expectation& e : sens::parse_baseline(in)) {
                bool wanted = only.empty();
                for (const std::string& o : only)
                    wanted = wanted || o == e.scenario;
                if (wanted)
                    expect.push_back(e);
            }
            for (const std::string& o : only)
                sens::scenario(o);  // unknown name: error
            if (expect.empty())
                throw std::runtime_error("no expectations for these scenarios in " + baseline);
            const auto verdicts = sens::check_baseline(expect, t, seed, threads, isa);
            if (!csv.empty()) {
                std::ofstream out(csv);
                out << "scenario,snr_db,metric,bound,limit,measured,pass\n";
                for (const sens::Verdict& v : verdicts)
                    out << v.expectation.scenario << "," << v.expectation.snr_db << "," << v.expectation.metric << ","
                        << (v.expectation.is_min ? "min" : "max") << "," << v.expectation.limit << "," << v.measured
                        << "," << (v.pass ? 1 : 0) << "\n";
            }
            int failed = 0;
            for (const sens::Verdict& v : verdicts) {
                const auto& e = v.expectation;
                std::cout << (v.pass ? "pass " : "FAIL ") << e.scenario << " at " << e.snr_db << " dB: " << e.metric
                          << " = " << v.measured << (e.is_min ? " >= " : " <= ") << e.limit << "\n";
                failed += v.pass ? 0 : 1;
            }
            std::cout << verdicts.size() - static_cast<std::size_t>(failed) << " of " << verdicts.size()
                      << " expectations met (tier " << t.name << ")\n";
            return failed ? 1 : 0;
        }

        std::vector<sens::SweepPoint> points;
        for (const sens::Scenario& s : sens::scenarios()) {
            bool wanted = only.empty();
            for (const std::string& o : only)
                wanted = wanted || o == s.name;
            if (wanted)
                for (double v : t.snr_db)
                    points.push_back({s, v});
        }
        for (const std::string& o : only)
            sens::scenario(o);  // unknown name: error
        const auto results = sens::run_sweep(points, t, seed, threads, isa);
        if (!csv.empty()) {
            std::ofstream out(csv);
            sens::write_csv(out, results);
        }
        std::ostringstream md;
        sens::write_markdown(md, results, t, seed);
        if (!markdown.empty())
            std::ofstream(markdown) << md.str();
        if (!doc.empty())
            return update_doc(doc, md.str());
        if (csv.empty() && markdown.empty())
            std::cout << md.str();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "yantra-sens: " << e.what() << "\n";
        return 2;
    }
}
