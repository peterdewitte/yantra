// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra 68ea20a: plugins/tetra/search/include/yantra/tetra/
// search/carrier_search.h, adapted for RAD-102, RAD-110, RAD-111, RAD-23
// (pinned carriers for the CARRIERS plan, TETRA band table).
//
// TETRA carrier search in a wideband capture (RAD-102, RAD-111, RAD-23):
// an averaged FFT spectrum (spectrum.hpp) gives candidate carriers on the
// 25 kHz raster including the +6.25, -6.25 and +12.5 kHz offsets of the
// MAC-SYSINFO offset field (EN 300 392-2 21.4.4.1; a -12.5 kHz carrier is
// the +12.5 kHz offset of the raster channel below), so on a 6.25 kHz
// grid. Each candidate gets its own channelizer (72 kHz) and decoder
// (CarrierHandler); it is verified by its training sequences and locked
// once its BSCH (MAC-SYNC) decodes. A candidate without training
// sequences is retried 6.25 kHz to either side (a carrier between two
// grid points of the spectrum peak). Locked carriers are watched: no
// training sequences for loss_s makes them lost; a lost carrier that
// comes back within reacquire_s is locked again, else it is dropped and
// the spectrum is searched again (re-search on loss). Everything is
// counted in samples of the capture, never in wall-clock time.
//
// Pinned carriers (pin(), e.g. the CARRIERS plan or a given serving
// carrier) get a decoder at once and are never dropped: they go between
// candidate, locked and lost by themselves. With
// SearchConfig::search_spectrum false only pinned carriers are decoded.
//
// Captures narrower than a band: sweep_centres() plans the retune steps
// (band scan, RAD-23) and cover_centres() the fewest centres that hold a
// list of carriers (neighbour levels by retuning, RAD-112). The plugins
// do the retuning; tetra_bands() lists the bands of RAD-110.
#pragma once

#include "yantra/receiver/channelizer.hpp"
#include "yantra/receiver/kernels.hpp"
#include "yantra/receiver/spectrum.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace yantra::receiver::tetra {

inline constexpr double kRasterHz = 25000.0;
inline constexpr double kGridHz = 6250.0;

/// A TETRA frequency range a radio tunes (RAD-110).
struct TetraBand {
    double first_hz = 0.0;
    double last_hz = 0.0;
    const char* name = "";
};
/// The TETRA ranges of RAD-110: 350-470 MHz (e.g. 380-400 MHz public
/// safety) and 790-920 MHz (800 MHz TETRA).
const std::vector<TetraBand>& tetra_bands();
/// The range of tetra_bands() holding `frequency_hz`, if any.
std::optional<TetraBand> tetra_band_of(double frequency_hz);

/// Carrier offset in Hz of a MAC-SYSINFO offset code (0: 0, 1: +6.25 kHz,
/// 2: -6.25 kHz, 3: +12.5 kHz).
double offset_of_code(int code);

/// A frequency on the 6.25 kHz grid as raster channel + offset code.
struct RasterPoint {
    double channel_hz = 0.0;  ///< the 25 kHz raster channel
    int offset_code = 0;      ///< SYSINFO offset code
    double frequency_hz() const { return channel_hz + offset_of_code(offset_code); }
};
/// Nearest grid point of `frequency_hz` (-12.5 kHz goes to +12.5 kHz of
/// the channel below).
RasterPoint raster_point(double frequency_hz);

/// Cell identity of a locked carrier (broadcast only, no addresses).
struct CellIdentity {
    uint16_t mcc = 0;
    uint16_t mnc = 0;
    uint8_t colour_code = 0;
    std::optional<uint16_t> location_area;  ///< once a SYSINFO decoded
    bool air_encryption = false;            ///< SYSINFO service details
    bool operator==(const CellIdentity&) const = default;
};

/// What a carrier's decoder has seen so far (running totals).
struct Observation {
    uint64_t slots = 0;        ///< slot positions passed while burst-locked or searching
    uint64_t slots_found = 0;  ///< with a training sequence
    std::optional<CellIdentity> cell;
    double level_dbfs = -300.0;  ///< channel level of the last period, dBFS
    double mer_db = 0.0;
};

/// The decoder of one carrier: channel IQ at 72 kHz in, observations out.
class CarrierHandler {
public:
    virtual ~CarrierHandler() = default;
    virtual void process(const cf* x, std::size_t n) = 0;
    virtual Observation observe() const = 0;
};

/// Creates the handler of a carrier at `frequency_hz`; its channel sample 0
/// is the signal of capture sample `first_sample` + `offset_s` seconds
/// (start-up transient skipped, channelizer delay removed).
using HandlerFactory =
    std::function<std::unique_ptr<CarrierHandler>(double frequency_hz, uint64_t first_sample, double offset_s)>;

/// The default handler: CarrierDecoder, nothing else.
std::unique_ptr<CarrierHandler> make_decoder_handler(Isa isa = Isa::Simd);

enum class CarrierState {
    Candidate,  ///< energy found, being verified
    Locked,     ///< BSCH decoded, clear cell
    Encrypted,  ///< BSCH decoded, the cell uses air-interface encryption
    Lost,       ///< was locked, no training sequences now
};
const char* state_name(CarrierState s);  ///< "candidate", "locked", ...

struct Carrier {
    uint64_t id = 0;  ///< running number in this search
    double frequency_hz = 0.0;
    RasterPoint raster;
    CarrierState state = CarrierState::Candidate;
    double spectrum_dbfs = -300.0;  ///< level in the spectrum (25 kHz), dBFS
    Observation obs;
    uint64_t since_sample = 0;  ///< capture sample of the last state change
    bool pinned = false;        ///< never dropped (pin())
};

/// A carrier that changed: new, new state, new cell, or removed.
struct CarrierEvent {
    enum class Kind { Added, StateChanged, CellChanged, Removed } kind = Kind::Added;
    Carrier carrier;
    std::string reason;  ///< for Removed: "no training sequence", "lost", "retune"
    uint64_t sample = 0; ///< capture sample
};

/// Level of one 25 kHz raster channel (RF-04).
struct ChannelLevel {
    double channel_hz = 0.0;
    double level_dbfs = -300.0;  ///< power in the channel (+/-12.5 kHz), dBFS
    std::optional<Carrier> carrier;  ///< a carrier on this channel or its offsets
};

struct SearchConfig {
    Isa isa = Isa::Simd;
    double usable_fraction = 0.8;   ///< share of the capture searched (anti-alias edges)
    double spectrum_seconds = 0.1;  ///< averaging of one spectrum
    double search_interval_s = 0.5; ///< spectrum searches this often
    double margin_db = 8.0;         ///< candidate: this far above the noise floor
    double verify_s = 1.0;          ///< training sequences must appear within this
    double min_found_ratio = 0.3;   ///< share of slots with a training sequence
    double lock_s = 2.5;            ///< BSCH must decode within this (a BSCH every 1.02 s)
    double loss_s = 0.5;            ///< locked: no training sequences this long = lost
    double reacquire_s = 3.0;       ///< lost this long = dropped, searched again
    double retry_s = 5.0;           ///< a refused grid point is searched again after this
    std::size_t max_carriers = 64;  ///< decoders at the same time (pinned ones included)
    /// Search the spectrum for carriers (false: only pinned carriers).
    bool search_spectrum = true;
};

class CarrierSearch {
public:
    /// `factory` null = make_decoder_handler().
    explicit CarrierSearch(SearchConfig config = {}, HandlerFactory factory = {});
    ~CarrierSearch();
    CarrierSearch(const CarrierSearch&) = delete;
    CarrierSearch& operator=(const CarrierSearch&) = delete;

    /// Feeds capture samples at `centre_hz` / `rate_hz`. A changed centre
    /// or rate, or `discontinuity`, drops every carrier (Removed, "retune")
    /// and starts over. Appends what changed to `events`.
    void process(const cf* x, std::size_t n, double centre_hz, double rate_hz, bool discontinuity,
                 std::vector<CarrierEvent>& events);

    /// Decodes `frequency_hz` from the next process() on and never drops
    /// it (CARRIERS plan, a given serving carrier). False when it is
    /// outside the span of the current capture (or no capture yet: then it
    /// is pinned once the first process() sets the span) or the decoder
    /// limit is reached. A carrier already found there becomes pinned.
    bool pin(double frequency_hz);
    /// Drops every pinned carrier (Removed, "unpinned").
    void unpin_all(std::vector<CarrierEvent>& events);

    const std::vector<Carrier>& carriers() const { return carriers_; }
    /// Runs body(0..count-1), possibly in parallel threads, and returns
    /// when all are done (the measurement engine's worker pool).
    using ParallelFor = std::function<void(std::size_t count, const std::function<void(std::size_t)>& body)>;
    /// The carriers' channelizers and handlers run through `executor`
    /// (null: one after the other in the calling thread). Carriers share
    /// no state, so the results do not depend on it.
    void set_executor(ParallelFor executor) { executor_ = std::move(executor); }
    /// The handler of a carrier (for its records), nullptr if none.
    CarrierHandler* handler(uint64_t carrier_id) const;
    /// Levels of every raster channel inside the searched span, from the
    /// last spectrum, with the carrier found on each (RF-04).
    std::vector<ChannelLevel> channel_levels() const;
    /// Level (dBFS, 25 kHz) of any frequency in the last spectrum; empty
    /// when outside the searched span (RF-03 neighbour levels).
    std::optional<double> level_at(double frequency_hz) const;
    /// Searched span: [low, high] carrier centres.
    std::pair<double, double> span() const;
    double noise_floor_dbfs() const { return noise_dbfs_; }
    uint64_t samples() const { return samples_; }
    uint64_t searches() const { return searches_; }
    const SearchConfig& config() const { return cfg_; }

private:
    struct Slot;
    void restart(double centre_hz, double rate_hz, std::vector<CarrierEvent>& events);
    void search(std::vector<CarrierEvent>& events);
    bool add_candidate(double frequency_hz, double dbfs, std::vector<CarrierEvent>& events, bool pinned = false);
    void update_states(std::vector<CarrierEvent>& events);
    bool occupied(double frequency_hz, double min_distance_hz) const;

    SearchConfig cfg_;
    HandlerFactory factory_;
    double centre_hz_ = 0.0;
    double rate_hz_ = 0.0;
    uint64_t samples_ = 0;
    uint64_t next_search_ = 0;
    uint64_t searches_ = 0;
    uint64_t next_id_ = 1;
    std::unique_ptr<SpectrumAnalyzer> spectrum_;
    SpectrumFrame last_;
    double noise_dbfs_ = -300.0;
    std::vector<Carrier> carriers_;
    std::vector<std::unique_ptr<Slot>> slots_;  ///< parallel to carriers_
    struct Refused {
        double frequency_hz;
        uint64_t until_sample;
    };
    std::vector<Refused> refused_;
    std::vector<float> noise_work_;
    std::vector<double> pins_;  ///< pinned frequencies waiting for a span
    std::vector<CarrierEvent> pin_events_;  ///< Added events of pin(), sent with the next process()
    ParallelFor executor_;
};

/// Retune plan of a scanner sweep (RF-04): capture centres whose usable
/// parts (usable_fraction of the rate, minus half a channel at each edge)
/// together hold every raster channel from first_hz to last_hz once.
std::vector<double> sweep_centres(double first_hz, double last_hz, double rate_hz, double usable_fraction = 0.8);
/// The fewest capture centres that hold all `frequencies_hz` in their
/// usable part (e.g. neighbour carriers to measure by retuning, RF-03).
std::vector<double> cover_centres(std::vector<double> frequencies_hz, double rate_hz,
                                  double usable_fraction = 0.8);

}  // namespace yantra::receiver::tetra
