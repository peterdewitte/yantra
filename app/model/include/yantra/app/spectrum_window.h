// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tools > Spectrum (UI-24; radios RAD-140, radio API 1.2): the model of
// the window, without drawing. For the chosen radio with the SPECTRUM
// capability (the simulator, HackRF and other SDR plugins; never the PEI
// terminals or the beacon) it shows the spectrum of the TETRA band (power
// per frequency, dBFS or dBm as the radio delivers it, calibrated dBFS as
// dBm, RF-02) and a waterfall of about the last 120 s, with the serving
// and neighbour carriers of the current measurement marked. The band is
// set in MHz (default 380-400), clipped to the radio's plan ranges; an
// SDR delivers only the part within the span it receives at once (radio
// API definition 11.2/5), which the window says.
//
// Spectra are asked for only while the window is open (LiveSurvey::
// request_spectrum) and are never recorded (record files only name the
// capability). Power per bin only: no IQ here (RAD-26).
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "yantra/app/survey_monitor.h"
#include "yantra/core/radio/radio_host.h"
#include "yantra/core/radio/spectrum.h"

namespace yantra::app {

class LiveSurvey;

/// The band the user asked for, in MHz (the window's two fields).
struct SpectrumForm {
    double first_mhz = 380.0;
    double last_mhz = 400.0;
};

/// The asked band clipped to the radio's plan ranges.
struct SpectrumBand {
    std::uint64_t first_hz = 0;
    std::uint64_t last_hz = 0;
    bool valid = true;    ///< first < last within 1..10000 MHz
    bool clipped = false; ///< narrowed to the radio's ranges
    std::string note;     ///< why it was clipped or refused ("" none)
};
SpectrumBand clip_band(const SpectrumForm& form, const std::optional<radio::PlanRanges>& ranges);

/// A carrier marked on the spectrum: the serving cell and its neighbours.
struct SpectrumMarker {
    double frequency_hz = 0.0;
    std::string label;  ///< the cell's name, else its identity, else the frequency
    bool serving = false;
};
std::vector<SpectrumMarker> spectrum_markers(const SurveyFrame& frame);

/// "dBm" or "dBFS" (calibrated dBFS is dBm).
const char* spectrum_unit(const radio::Spectrum& s);
/// The flags shown: SIMULATED, OVERLOAD, UNCALIBRATED (dBFS without a
/// calibration, or the radio says so).
std::vector<std::string> spectrum_flags(const radio::Spectrum& s);
/// The note when the spectrum covers less than the asked band (an SDR's
/// instantaneous span); "" when it covers it.
std::string coverage_note(const radio::Spectrum& s, std::uint64_t first_hz, std::uint64_t last_hz);

/// The colour scale of the waterfall: from about the noise (10th
/// percentile of the latest spectra) to their strongest bin, at least 20 dB.
std::pair<double, double> colour_range(const std::vector<std::shared_ptr<const radio::Spectrum>>& history);
/// The app's colour map (the coverage map's red-orange-yellow-green, below
/// it a dark blue for the noise) as a gradient; t in 0..1. 0xRRGGBB.
std::uint32_t waterfall_rgb(double t);

/// The waterfall as RGBA rows (newest at the top): `columns` over
/// first_hz..last_hz, `rows` over the last `span_s` seconds before the
/// newest spectrum; a row without a spectrum is transparent.
std::vector<std::uint8_t> waterfall_pixels(const std::vector<std::shared_ptr<const radio::Spectrum>>& history,
                                           double first_hz, double last_hz, int columns, int rows,
                                           double min_db, double max_db, double span_s = 120.0);

/// What the window shows this frame.
struct SpectrumView {
    enum class State {
        NoProject,     ///< no project: no radio (the radios belong to a project)
        NoRadio,       ///< the radios were not searched or none is chosen
        NoCapability,  ///< the chosen radio has no SPECTRUM (hidden, with why)
        NotMeasuring,  ///< the radio does not measure: Start radio
        Waiting,       ///< asked, no spectrum yet
        Showing,
    };
    State state = State::NoProject;
    std::string message;   ///< the explanation of a state other than Showing
    std::string radio;     ///< the radio's label
    SpectrumBand band;
    std::string coverage_note;
    std::string pending_note;  ///< LiveSurvey::spectrum_note()
    std::shared_ptr<const radio::Spectrum> latest;
    std::vector<std::shared_ptr<const radio::Spectrum>> history;
    std::uint64_t received = 0;  ///< changes with every new spectrum
    std::string unit;
    std::vector<std::string> flags;
    std::vector<SpectrumMarker> markers;
    double min_db = -120.0;
    double max_db = -40.0;
};

/// Asks `live` for the spectra of the form's band (the window is open)
/// and builds the view; nullptr `live`: no project.
SpectrumView spectrum_view(LiveSurvey* live, const SurveyFrame& frame, const SpectrumForm& form);

}  // namespace yantra::app
