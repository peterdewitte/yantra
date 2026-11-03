// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/receiver/pi4dqpsk.hpp"

#include "yantra/receiver/window.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace yantra::receiver {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr int kBlockSymbols = 8;  // chunk of the chain and FLL update period

const Pi4DqpskConfig& checked(const Pi4DqpskConfig& c)
{
    if (!(c.symbol_rate_hz > 0.0))
        throw std::invalid_argument("pi/4-DQPSK: symbol rate must be positive");
    if (c.samples_per_symbol < 2)
        throw std::invalid_argument("pi/4-DQPSK: at least 2 samples per symbol are needed");
    if (!(c.rolloff > 0.0) || c.rolloff > 1.0)
        throw std::invalid_argument("pi/4-DQPSK: roll-off must be in (0, 1]");
    if (c.rrc_span_symbols < 2 || c.rrc_span_symbols % 2 != 0)
        throw std::invalid_argument("pi/4-DQPSK: RRC span must be even and >= 2");
    if (c.coarse_cfo && c.coarse_symbols < 128)
        throw std::invalid_argument("pi/4-DQPSK: coarse estimate needs >= 128 symbols");
    if (c.max_cfo_hz < 0.0 ||
        (c.max_cfo_hz > 0.0 &&
         (1.0 + c.rolloff) / 2.0 * c.symbol_rate_hz + c.max_cfo_hz >= c.samples_per_symbol * c.symbol_rate_hz / 2.0))
        throw std::invalid_argument("pi/4-DQPSK: max_cfo_hz does not fit the sample rate");
    if (c.mer_symbols == 0 || !(c.unlock_mer_db <= c.lock_mer_db))
        throw std::invalid_argument("pi/4-DQPSK: bad MER/lock settings");
    return c;
}

// Second-order loop gains (proportional, integral) for noise bandwidth
// bn (fraction of the update rate), damping 1/sqrt(2), detector gain kd.
void loop_gains(double bn, double kd, double& kp, double& ki)
{
    const double zeta = std::numbers::sqrt2 / 2.0;
    const double theta = bn / (zeta + 0.25 / zeta);
    const double den = 1.0 + 2.0 * zeta * theta + theta * theta;
    kp = 4.0 * zeta * theta / den / kd;
    ki = 4.0 * theta * theta / den / kd;
}

double wrap_pi(double a)
{
    while (a > kPi)
        a -= 2.0 * kPi;
    while (a < -kPi)
        a += 2.0 * kPi;
    return a;
}

}  // namespace

Pi4DqpskDemodulator::Pi4DqpskDemodulator(const Pi4DqpskConfig& config)
    : config_(checked(config)),
      sps_(config.samples_per_symbol),
      max_cfo_(config.max_cfo_hz > 0.0 ? config.max_cfo_hz : config.symbol_rate_hz / 6.0),
      k_(&kernels(config.isa)),
      rrc_(rrc_taps(config.rolloff, config.samples_per_symbol, config.rrc_span_symbols), 1, config.isa),
      nco_(0.0, config.samples_per_symbol * config.symbol_rate_hz, config.isa),
      rrc_delay_(0.5 * config.rrc_span_symbols * config.samples_per_symbol),
      // Measured Gardner slope at unit power: ~1.0 per symbol of timing
      // error (1.02 for roll-off 0.35), i.e. 1/sps per sample.
      kd_(1.0 / config.samples_per_symbol)
{
    reset();
}

void Pi4DqpskDemodulator::reset()
{
    rrc_.reset();
    nco_.reset();
    coarse_pending_ = config_.coarse_cfo;
    pending_.clear();
    input_pos_ = 0;
    mixed_.clear();
    y_.clear();
    y_base_ = 0;
    t_ = static_cast<double>(sps_);
    agc_power_ = 0.0;
    integ_ = 0.0;
    rate_ppm_ = 0.0;
    reacquire_ = false;
    fll_acquire_left_ = config_.coarse_cfo ? 0 : config_.acquisition_symbols;
    roll_.clear();
    freq_ = 0.0;
    fll_acc_ = {};
    fll_count_ = 0;
    z_prev_ = {};
    have_prev_ = false;
    theta_ = 0.0;
    amp_ = 1.0;
    sig_acc_ = 0.0;
    err_acc_ = 0.0;
    status_ = {};
}

void Pi4DqpskDemodulator::process(const cf* in, std::size_t n, std::vector<DemodSymbol>& out)
{
    if (coarse_pending_) {
        pending_.insert(pending_.end(), in, in + n);
        if (pending_.size() < config_.coarse_symbols * static_cast<std::size_t>(sps_))
            return;
        freq_ = estimate_coarse(pending_.data());
        status_.coarse_offset_hz = freq_;
        status_.frequency_offset_hz = freq_;
        fll_acquire_left_ = config_.acquisition_symbols;
        coarse_pending_ = false;
        std::vector<cf> buffered;
        buffered.swap(pending_);
        run(buffered.data(), buffered.size(), out);
        return;
    }
    run(in, n, out);
}

double Pi4DqpskDemodulator::spectral_centroid(const cf* x) const
{
    // Averaged Hann periodograms (64 symbols each, half overlap) of the
    // buffered samples; centroid of the power above the noise floor within
    // the signal band widened by the search range.
    const std::size_t seg = static_cast<std::size_t>(64 * sps_);
    Fft fft(seg);
    const std::vector<float> w = make_window(Window::Hann, seg);
    std::vector<float> p(seg, 0.0f);
    std::vector<cf> buf(seg);
    // Exactly coarse_symbols, however the input was split.
    const std::size_t used = config_.coarse_symbols * static_cast<std::size_t>(sps_);
    for (std::size_t start = 0; start + seg <= used; start += seg / 2) {
        k_->apply_window(x + start, w.data(), buf.data(), seg);
        fft.forward(buf.data());
        k_->accumulate_power(buf.data(), p.data(), seg);
    }
    std::vector<float> sorted = p;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t low = std::max<std::size_t>(1, seg / 5);
    double floor = 0.0;
    for (std::size_t i = 0; i < low; ++i)
        floor += sorted[i];
    floor /= static_cast<double>(low);
    const double fs = sps_ * config_.symbol_rate_hz;
    const double half_band = 0.5 * (1.0 + config_.rolloff) * config_.symbol_rate_hz + max_cfo_;
    double num = 0.0;
    double den = 0.0;
    for (std::size_t k = 0; k < seg; ++k) {
        const double f = (k < seg / 2 ? static_cast<double>(k) : static_cast<double>(k) - static_cast<double>(seg)) *
                         fs / static_cast<double>(seg);
        if (std::fabs(f) > half_band)
            continue;
        const double v = std::max(0.0, static_cast<double>(p[k]) - floor);
        num += f * v;
        den += v;
    }
    return den > 0.0 ? std::clamp(num / den, -max_cfo_, max_cfo_) : 0.0;
}

double Pi4DqpskDemodulator::estimate_coarse(const cf* x) const
{
    // Candidate offsets every symbol_rate / 12 over +/- max_cfo. For each:
    // mix, matched filter (short RRC), then the 4th power of the
    // one-symbol differential product, which is the same (-1) for every
    // pi/4-DQPSK step and turns into e^(j 8 pi f T) with a residual offset
    // f. The candidate with the most coherent sum wins and its phase gives
    // the residual (unambiguous within +/- symbol_rate / 8). The matched
    // filter removes adjacent carriers first, so a much stronger
    // neighbour, whose skirt reaches into this channel's band, does not
    // pull the estimate (a spectral centroid of the band was pulled).
    const std::size_t used = config_.coarse_symbols * static_cast<std::size_t>(sps_);
    const double fs = sps_ * config_.symbol_rate_hz;
    const double step = config_.symbol_rate_hz / 12.0;
    const int n = static_cast<int>(std::ceil(max_cfo_ / step));
    const std::vector<float> taps = rrc_taps(config_.rolloff, sps_, 8);
    std::vector<cf> mixed(used);
    std::vector<cf> y;
    struct Candidate {
        double metric;
        double f0;
        double residual;
    };
    std::vector<Candidate> cands;
    double best_metric = -1.0;
    for (int i = -n; i <= n; ++i) {
        const double f0 = std::clamp(i * step, -max_cfo_, max_cfo_);
        Nco nco(-f0, fs, config_.isa);
        nco.mix(x, mixed.data(), used);
        FirFilter rrc(taps, 1, config_.isa);
        y.clear();
        rrc.process(mixed.data(), used, y);
        std::complex<double> acc{};
        double power = 0.0;
        for (std::size_t k = taps.size() + static_cast<std::size_t>(sps_); k < y.size(); ++k) {
            const std::complex<double> d =
                std::complex<double>(y[k]) * std::conj(std::complex<double>(y[k - static_cast<std::size_t>(sps_)]));
            const std::complex<double> d2 = d * d;
            acc += d2 * d2;
            power += std::norm(d2);
        }
        const double metric = power > 0.0 ? std::abs(acc) / power : 0.0;
        cands.push_back({metric, f0, std::arg(-acc) * config_.symbol_rate_hz / (8.0 * std::numbers::pi)});
        best_metric = std::max(best_metric, metric);
    }
    // The residual is only unambiguous within +/- symbol_rate / 8, so a
    // candidate far from the carrier can report an alias a quarter symbol
    // rate off; at a high SNR every candidate's metric is about the same
    // and such a one may win by a hair. Among the candidates close to the
    // best metric, the one with the smallest residual is the one nearest
    // the carrier (always within half a step).
    double best = 0.0;
    double best_residual = std::numeric_limits<double>::infinity();
    for (const Candidate& c : cands)
        if (c.metric >= 0.9 * best_metric && std::fabs(c.residual) < best_residual) {
            best_residual = std::fabs(c.residual);
            best = c.f0 + c.residual;
        }
    // Clean signals give ~0.57 (all sample phases, so with ISI); at low
    // SNR (Eb/N0 < ~10 dB) the 4th power is too noisy and the spectral
    // centroid, which a strong neighbour only pulls at a high SNR anyway,
    // is the better estimate.
    if (best_metric >= 0.45)
        return std::clamp(best, -max_cfo_, max_cfo_);
    return spectral_centroid(x);
}

void Pi4DqpskDemodulator::run(const cf* in, std::size_t n, std::vector<DemodSymbol>& out)
{
    // Whole blocks of kBlockSymbols symbols, counted from input 0, so the
    // result does not depend on how the input is split into calls.
    const std::size_t block = static_cast<std::size_t>(kBlockSymbols * sps_);
    std::size_t pos = 0;
    while (pos < n) {
        const std::size_t m = std::min(n - pos, block - mixed_.size());
        mixed_.insert(mixed_.end(), in + pos, in + pos + m);
        pos += m;
        if (mixed_.size() < block)
            break;
        // While unlocked (after the first acquisition), estimate the offset
        // again over every coarse window of raw input; a large difference
        // (the FLL wandered in a gap, or a new carrier) restarts the FLL
        // there. Small differences are left to the FLL.
        if (config_.coarse_cfo && !status_.locked && status_.symbols >= config_.acquisition_symbols) {
            roll_.insert(roll_.end(), mixed_.begin(), mixed_.end());
            if (roll_.size() >= config_.coarse_symbols * static_cast<std::size_t>(sps_)) {
                const double est = estimate_coarse(roll_.data());
                if (std::fabs(est - freq_) > config_.symbol_rate_hz / 12.0) {
                    freq_ = est;
                    status_.coarse_offset_hz = est;
                    fll_acquire_left_ = config_.acquisition_symbols;
                }
                roll_.clear();
            }
        } else {
            roll_.clear();
        }
        nco_.set_frequency(-freq_);
        nco_.mix(mixed_.data(), mixed_.data(), block);
        const std::size_t first = y_.size();
        rrc_.process(mixed_.data(), block, y_);
        mixed_.clear();
        input_pos_ += block;
        double p = 0.0;
        for (std::size_t i = first; i < y_.size(); ++i)
            p += std::norm(y_[i]);
        p /= static_cast<double>(y_.size() - first);
        // Block AGC: fast attack (a rising level, e.g. the filters of a
        // channelizer filling up, is followed at once), ~64-symbol decay.
        agc_power_ = p > agc_power_ ? p : agc_power_ + (p - agc_power_) * (1.0 / 8.0);
        symbols(out);
    }
}

cf Pi4DqpskDemodulator::interpolate(double t) const
{
    const auto i = static_cast<std::size_t>(std::floor(t));
    const float mu = static_cast<float>(t - std::floor(t));
    // Cubic Lagrange on y[i-1..i+2].
    const float cm1 = -mu * (mu - 1.0f) * (mu - 2.0f) / 6.0f;
    const float c0 = (mu + 1.0f) * (mu - 1.0f) * (mu - 2.0f) / 2.0f;
    const float c1 = -(mu + 1.0f) * mu * (mu - 2.0f) / 2.0f;
    const float c2 = (mu + 1.0f) * mu * (mu - 1.0f) / 6.0f;
    return y_[i - 1] * cm1 + y_[i] * c0 + y_[i + 1] * c1 + y_[i + 2] * c2;
}

void Pi4DqpskDemodulator::symbols(std::vector<DemodSymbol>& out)
{
    if (!(agc_power_ > 0.0))
        return;
    const double sps = static_cast<double>(sps_);
    const double half = 0.5 * sps;
    // Floored: after a long stretch of zeros (signal off) the decaying AGC
    // power reaches denormals and 1/sqrt would overflow a float (0 * inf
    // = NaN symbols).
    const float norm = static_cast<float>(1.0 / std::sqrt(std::max(agc_power_, 1e-24)));
    const double beta = 1.0 / static_cast<double>(config_.mer_symbols);
    while (t_ + 2.0 < static_cast<double>(y_.size())) {
        // Wide loops for the first acquisition_symbols and again after a
        // lost lock until locked again (not while never locked: at low SNR
        // the narrow loops give the lower error rate).
        const bool acquiring = status_.symbols < config_.acquisition_symbols || reacquire_;
        const cf z = interpolate(t_) * norm;
        double adj = integ_;
        if (have_prev_) {
            // Gardner: > 0 when the strobes are late.
            const cf m = interpolate(t_ - half) * norm;
            // Limited, so a level step cannot throw the loop far off.
            const double e = std::clamp(static_cast<double>(((z - z_prev_) * std::conj(m)).real()), -2.0, 2.0);
            double kp = 0.0;
            double ki = 0.0;
            loop_gains(acquiring ? config_.timing_bw_acquire : config_.timing_bw_track, kd_, kp, ki);
            integ_ = std::clamp(integ_ + ki * e, -0.01 * sps, 0.01 * sps);  // +/-1 %
            adj = std::clamp(kp * e + integ_, -0.5 * sps, 0.5 * sps);
        }

        DemodSymbol s;
        s.index = status_.symbols;
        s.sample_pos = static_cast<double>(y_base_) + t_ - rrc_delay_;

        // Differential product and FLL (4th power removes the modulation).
        const cf d = have_prev_ ? z * std::conj(z_prev_) : cf{};
        const float dn = std::norm(d);
        if (dn > 0.0f) {
            const cf d2 = d * d;
            fll_acc_ -= d2 * d2 / dn;
        }
        if (++fll_count_ == kBlockSymbols) {
            if (std::norm(fll_acc_) > 0.0f) {
                const double err_hz = std::arg(fll_acc_) * config_.symbol_rate_hz / (8.0 * kPi);
                freq_ += (fll_acquire_left_ > 0 ? config_.fll_gain_acquire : config_.fll_gain_track) * err_hz;
                const double lim = max_cfo_ + config_.symbol_rate_hz / 8.0;
                freq_ = std::clamp(freq_, -lim, lim);
            }
            fll_acc_ = {};
            fll_count_ = 0;
        }

        // Quality: phase tracking on the 8 points, MER, amplitude.
        const cf zr = z * std::polar(1.0f, static_cast<float>(-theta_));
        const double a = std::arg(zr);
        const double ideal_angle = std::round(a / (kPi / 4.0)) * (kPi / 4.0);
        theta_ = wrap_pi(theta_ + config_.phase_gain * wrap_pi(a - ideal_angle));
        const double mag = std::abs(zr);
        amp_ += (mag - amp_) * (1.0 / 64.0);
        const cf ideal = std::polar(static_cast<float>(amp_), static_cast<float>(ideal_angle));
        sig_acc_ += (amp_ * amp_ - sig_acc_) * beta;
        err_acc_ += (static_cast<double>(std::norm(zr - ideal)) - err_acc_) * beta;
        status_.mer_db = err_acc_ > 0.0 ? 10.0 * std::log10(sig_acc_ / err_acc_) : 99.0;

        const float inv = static_cast<float>(1.0 / std::max(amp_, 1e-9));
        s.z = zr * inv;
        s.ideal = std::polar(1.0f, static_cast<float>(ideal_angle));
        const cf dsoft = d * (inv * inv);
        s.soft[0] = dsoft.imag();
        s.soft[1] = dsoft.real();
        // Sample-clock error: the loop integrator, smoothed over ~1000
        // symbols (its own noise is a few tens of ppm at moderate SNR).
        rate_ppm_ += (-integ_ / sps * 1e6 - rate_ppm_) * (acquiring ? 1.0 / 64.0 : 1.0 / 1024.0);
        status_.timing_rate_ppm = rate_ppm_;
        // The first strobe only primes the detectors.
        if (have_prev_) {
            if (fll_acquire_left_ > 0)
                --fll_acquire_left_;
            out.push_back(s);
            ++status_.symbols;
        }
        if (!status_.locked && status_.symbols >= config_.acquisition_symbols && status_.mer_db >= config_.lock_mer_db) {
            status_.locked = true;
            reacquire_ = false;
        } else if (status_.locked && status_.mer_db < config_.unlock_mer_db) {
            status_.locked = false;
            reacquire_ = true;
        }
        z_prev_ = z;
        have_prev_ = true;
        t_ += sps - adj;
    }
    status_.frequency_offset_hz = freq_;
    // Keep what the next strobe and its mid point still need.
    const double keep_from = std::floor(t_ - half) - 2.0;
    if (keep_from > 0.0) {
        const auto drop = std::min(static_cast<std::size_t>(keep_from), y_.size());
        y_.erase(y_.begin(), y_.begin() + static_cast<std::ptrdiff_t>(drop));
        y_base_ += drop;
        t_ -= static_cast<double>(drop);
    }
}

}  // namespace yantra::receiver
