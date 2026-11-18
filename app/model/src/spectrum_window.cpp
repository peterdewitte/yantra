// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tools > Spectrum model (UI-24, RAD-140); see spectrum_window.h.

#include "yantra/app/spectrum_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>

#include <yantra/radio.h>

#include "yantra/app/i18n.h"
#include "yantra/app/live_survey.h"

namespace yantra::app {

namespace {

std::string mhz(double hz) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", hz / 1e6);
    return b;
}

}  // namespace

SpectrumBand clip_band(const SpectrumForm& form, const std::optional<radio::PlanRanges>& ranges) {
    SpectrumBand b;
    if (!(std::isfinite(form.first_mhz) && std::isfinite(form.last_mhz) && form.first_mhz >= 1.0 &&
          form.last_mhz <= 10000.0 && form.first_mhz < form.last_mhz)) {
        b.valid = false;
        b.note = tr("The band needs a first frequency below the last one (MHz).");
        return b;
    }
    double first = form.first_mhz * 1e6;
    double last = form.last_mhz * 1e6;
    if (ranges && !ranges->ranges.empty()) {
        // The range that overlaps the asked band most.
        double best = -1.0;
        double bf = 0.0;
        double bl = 0.0;
        for (const radio::FrequencyRange& r : ranges->ranges) {
            const double f = std::max(first, static_cast<double>(r.first_hz));
            const double l = std::min(last, static_cast<double>(r.last_hz));
            if (l - f > best) {
                best = l - f;
                bf = f;
                bl = l;
            }
        }
        if (best <= 0.0) {
            b.valid = false;
            b.note = trf("The radio cannot measure {}-{} MHz: its ranges are {}.",
                         {mhz(first), mhz(last), [&] {
                              std::string t;
                              for (const radio::FrequencyRange& r : ranges->ranges)
                                  t += (t.empty() ? "" : ", ") + mhz(static_cast<double>(r.first_hz)) + "-" +
                                       mhz(static_cast<double>(r.last_hz)) + " MHz";
                              return t;
                          }()});
            return b;
        }
        if (bf > first || bl < last) {
            b.clipped = true;
            b.note = trf("Clipped to {}-{} MHz, what the radio can measure.", {mhz(bf), mhz(bl)});
        }
        first = bf;
        last = bl;
    }
    b.first_hz = static_cast<std::uint64_t>(std::llround(first));
    b.last_hz = static_cast<std::uint64_t>(std::llround(last));
    return b;
}

std::vector<SpectrumMarker> spectrum_markers(const SurveyFrame& frame) {
    std::vector<SpectrumMarker> out;
    auto label = [](const CellRow& r) {
        if (!r.name.empty()) return r.name;
        if (!r.identity.empty()) return r.identity;
        return mhz(static_cast<double>(r.frequency_hz)) + " MHz";
    };
    if (!frame.valid) return out;
    if (frame.serving && frame.serving->frequency_hz != 0)
        out.push_back({static_cast<double>(frame.serving->frequency_hz), label(*frame.serving), true});
    for (const CellRow& n : frame.neighbours) {
        if (n.frequency_hz == 0) continue;
        if (frame.serving && n.frequency_hz == frame.serving->frequency_hz) continue;
        out.push_back({static_cast<double>(n.frequency_hz), label(n), false});
    }
    return out;
}

const char* spectrum_unit(const radio::Spectrum& s) { return s.dbm ? "dBm" : "dBFS"; }

std::vector<std::string> spectrum_flags(const radio::Spectrum& s) {
    std::vector<std::string> out;
    if (s.simulated || (s.flags & YR_RECORD_SIMULATED) != 0) out.emplace_back("SIMULATED");
    if ((s.flags & YR_RECORD_OVERLOAD) != 0) out.emplace_back("OVERLOAD");
    if ((s.flags & YR_RECORD_UNCALIBRATED) != 0 || !s.dbm) out.emplace_back("UNCALIBRATED");
    return out;
}

std::string coverage_note(const radio::Spectrum& s, std::uint64_t first_hz, std::uint64_t last_hz) {
    if (first_hz == 0 || last_hz <= first_hz) return {};
    const double tol = std::max(s.bin_width_hz, 1000.0);
    if (s.start_hz <= static_cast<double>(first_hz) + tol && s.stop_hz >= static_cast<double>(last_hz) - tol) return {};
    return trf("This radio delivers {}-{} MHz of the band: an SDR shows only the span it receives at once, without "
               "retuning, so the measurement is never interrupted. Set a narrower band around the carriers you "
               "want to see.",
               {mhz(s.start_hz), mhz(s.stop_hz)});
}

std::pair<double, double> colour_range(const std::vector<std::shared_ptr<const radio::Spectrum>>& history) {
    std::vector<float> all;
    float top = -1e30F;
    // The newest ten spectra are enough for the scale.
    const std::size_t from = history.size() > 10 ? history.size() - 10 : 0;
    for (std::size_t i = from; i < history.size(); ++i) {
        for (float v : history[i]->power_db) {
            all.push_back(v);
            top = std::max(top, v);
        }
    }
    if (all.empty()) return {-120.0, -40.0};
    const std::size_t k = all.size() / 10;
    std::nth_element(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(k), all.end());
    double lo = std::floor(all[k] / 5.0) * 5.0 - 5.0;
    double hi = std::ceil(top / 5.0) * 5.0;
    if (hi - lo < 20.0) hi = lo + 20.0;
    return {lo, hi};
}

std::uint32_t waterfall_rgb(double t) {
    // Dark blue for the noise, then the coverage map's colours (survey
    // default_colour_scale: red, orange, yellow, green).
    static const std::uint32_t stops[] = {0x0d1b3e, 0x1e3a8a, 0xe53935, 0xfb8c00, 0xfdd835, 0x43a047};
    constexpr int n = static_cast<int>(sizeof stops / sizeof stops[0]);
    if (!std::isfinite(t)) t = 0.0;
    t = std::clamp(t, 0.0, 1.0) * (n - 1);
    const int i = std::min(static_cast<int>(t), n - 2);
    const double f = t - i;
    auto ch = [&](int shift) {
        const double a = (stops[i] >> shift) & 0xff;
        const double b = (stops[i + 1] >> shift) & 0xff;
        return static_cast<std::uint32_t>(std::lround(a + (b - a) * f));
    };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

std::vector<std::uint8_t> waterfall_pixels(const std::vector<std::shared_ptr<const radio::Spectrum>>& history,
                                           double first_hz, double last_hz, int columns, int rows, double min_db,
                                           double max_db, double span_s) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(std::max(columns, 0)) * std::max(rows, 0) * 4, 0);
    if (history.empty() || columns <= 0 || rows <= 0 || !(last_hz > first_hz) || !(span_s > 0.0)) return px;
    const std::int64_t newest = history.back()->monotonic_ns;
    const double row_s = span_s / rows;
    const double range = std::max(1e-9, max_db - min_db);
    for (const auto& s : history) {
        const double age_s = static_cast<double>(newest - s->monotonic_ns) / 1e9;
        if (age_s < 0.0 || age_s >= span_s || s->power_db.empty() || !(s->bin_width_hz > 0.0)) continue;
        // A spectrum fills the rows of its second (about one per second).
        const int top = static_cast<int>(age_s / row_s);
        const int bottom = std::min(rows, top + std::max(1, static_cast<int>(std::ceil(1.0 / row_s))));
        for (int c = 0; c < columns; ++c) {
            // The strongest bin under the column (a peak is never lost).
            const double f0 = first_hz + (last_hz - first_hz) * c / columns;
            const double f1 = first_hz + (last_hz - first_hz) * (c + 1) / columns;
            const double b0 = (f0 - s->start_hz) / s->bin_width_hz;
            const double b1 = (f1 - s->start_hz) / s->bin_width_hz;
            const long i0 = std::max(0L, static_cast<long>(std::floor(b0)));
            const long i1 = std::min(static_cast<long>(s->power_db.size()) - 1, static_cast<long>(std::ceil(b1)) - 1);
            if (i1 < i0 || b1 <= 0.0 || b0 >= static_cast<double>(s->power_db.size())) continue;
            float v = -1e30F;
            for (long i = i0; i <= i1; ++i) v = std::max(v, s->power_db[static_cast<std::size_t>(i)]);
            const std::uint32_t rgb = waterfall_rgb((v - min_db) / range);
            for (int r = top; r < bottom; ++r) {
                std::uint8_t* p = &px[(static_cast<std::size_t>(r) * columns + c) * 4];
                p[0] = static_cast<std::uint8_t>(rgb >> 16);
                p[1] = static_cast<std::uint8_t>(rgb >> 8);
                p[2] = static_cast<std::uint8_t>(rgb);
                p[3] = 255;
            }
        }
    }
    return px;
}

SpectrumView spectrum_view(LiveSurvey* live, const SurveyFrame& frame, const SpectrumForm& form) {
    SpectrumView v;
    if (live == nullptr) {
        v.message = tr("Open or create a project first: the radios measure for a project.");
        return v;
    }
    const RadioChoice* r = live->chosen();
    if (!live->radios_searched() || r == nullptr) {
        live->request_spectrum(std::nullopt);
        v.state = SpectrumView::State::NoRadio;
        v.message = live->radios_searched()
                        ? std::string(tr("No radio is chosen: choose one in the Walk tab (Select...)."))
                        : std::string(tr("Find the radios first (Walk tab > Find radios)."));
        return v;
    }
    v.radio = r->label;
    if (!r->spectrum) {
        live->request_spectrum(std::nullopt);
        v.state = SpectrumView::State::NoCapability;
        v.message = trf("{} delivers no spectrum: only radios with the SPECTRUM capability do (the simulator, "
                        "HackRF and other SDR radios), not TETRA terminals.",
                        {r->label});
        return v;
    }
    const bool own_device = live->connected() && live->device_plugin() == r->plugin_id && live->device_key() == r->device_key;
    v.band = clip_band(form, own_device ? live->plan_ranges() : std::nullopt);
    if (v.band.valid) live->request_spectrum(std::make_pair(v.band.first_hz, v.band.last_hz));
    else live->request_spectrum(std::nullopt);
    if (!live->radio_running()) {
        v.state = SpectrumView::State::NotMeasuring;
        v.message = trf("{} is not measuring: start it to see the spectrum.", {r->label});
        return v;
    }
    v.pending_note = live->spectrum_note();
    const auto store = live->spectra();
    v.latest = store->latest();
    v.history = store->history();
    v.received = store->received();
    v.markers = spectrum_markers(frame);
    if (!v.latest) {
        v.state = SpectrumView::State::Waiting;
        v.message = live->spectrum_active() ? std::string(tr("Waiting for the first spectrum (about one per second)..."))
                                            : v.pending_note;
        return v;
    }
    v.state = SpectrumView::State::Showing;
    v.unit = spectrum_unit(*v.latest);
    v.flags = spectrum_flags(*v.latest);
    v.coverage_note = coverage_note(*v.latest, v.band.first_hz, v.band.last_hz);
    std::tie(v.min_db, v.max_db) = colour_range(v.history);
    return v;
}

}  // namespace yantra::app
