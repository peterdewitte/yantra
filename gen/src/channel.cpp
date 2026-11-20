// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Channel model of the TETRA test-signal generator (RAD-72); see
// channel.hpp. Copied from yantra 68ea20a (core/src/sim/fading.cpp,
// impairments.cpp, propagation.cpp; plugins/tetra/sens/src/channel.cpp),
// adapted for RAD-72 (front-end impairments and sample drops added).

#include "yantra/gen/channel.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace yantra::gen {

// ------------------------------------------------------------ path loss --

double free_space_path_loss_db(double distance_m, double frequency_hz)
{
    const double d = std::max(distance_m, 0.01);
    return 20.0 * std::log10(4.0 * std::numbers::pi * d * frequency_hz / kSpeedOfLight);
}

double LogDistanceModel::loss_db(double distance_m, double frequency_hz) const
{
    const double pl0 = reference_loss_db ? *reference_loss_db
                                         : free_space_path_loss_db(reference_distance_m, frequency_hz);
    const double d = std::max(distance_m, reference_distance_m);
    return pl0 + 10.0 * exponent * std::log10(d / reference_distance_m);
}

ShadowingField::ShadowingField(double sigma_db, double decorrelation_m, uint64_t seed, int components)
    : sigma_(sigma_db), decorrelation_(decorrelation_m)
{
    if (!(sigma_db >= 0.0) || !std::isfinite(sigma_db) || !(decorrelation_m > 0.0) ||
        !std::isfinite(decorrelation_m) || components < 1)
        throw std::invalid_argument("shadowing needs sigma >= 0, decorrelation > 0, components >= 1");
    amplitude_ = sigma_db * std::sqrt(2.0 / components);
    if (sigma_db == 0.0)
        return;
    Rng rng(seed);
    waves_.reserve(static_cast<size_t>(components));
    for (int i = 0; i < components; ++i) {
        // The 2D spectrum of exp(-r / L) is proportional to
        // (1 + (k L)^2)^(-3/2); the radial wave number then has the CDF
        // 1 - (1 + (k L)^2)^(-1/2), inverted here.
        const double u = rng.uniform();
        const double q = 1.0 / (1.0 - u);
        const double k = std::sqrt(q * q - 1.0) / decorrelation_m;
        const double dir = rng.uniform(0.0, 2.0 * std::numbers::pi);
        const double phase = rng.uniform(0.0, 2.0 * std::numbers::pi);
        waves_.push_back({k * std::cos(dir), k * std::sin(dir), phase});
    }
}

double ShadowingField::value(double x_m, double y_m) const
{
    double s = 0.0;
    for (const auto& w : waves_)
        s += std::cos(w.kx * x_m + w.ky * y_m + w.phase);
    return amplitude_ * s;
}

// --------------------------------------------------------------- fading --

FadingType fading_type_from_string(const std::string& s)
{
    if (s == "none")
        return FadingType::none;
    if (s == "rayleigh")
        return FadingType::rayleigh;
    if (s == "rician")
        return FadingType::rician;
    throw std::invalid_argument("unknown fading type \"" + s + "\" (none, rayleigh, rician)");
}

const char* to_string(FadingType t)
{
    switch (t) {
    case FadingType::none:
        return "none";
    case FadingType::rayleigh:
        return "rayleigh";
    case FadingType::rician:
        return "rician";
    }
    return "none";
}

double doppler_hz(double speed_mps, double frequency_hz)
{
    return std::fabs(speed_mps) * frequency_hz / kSpeedOfLight;
}

FadingGenerator::FadingGenerator(const FadingSpec& spec, double doppler, uint64_t seed)
    : spec_(spec), doppler_(doppler)
{
    if (!(doppler >= 0.0) || !std::isfinite(doppler))
        throw std::invalid_argument("Doppler frequency must be >= 0");
    if (spec.type == FadingType::none)
        return;
    if (spec.sinusoids < 1 || spec.sinusoids > 4096)
        throw std::invalid_argument("fading needs 1 to 4096 sinusoids");
    if (!std::isfinite(spec.k_db))
        throw std::invalid_argument("Rician K must be finite");
    const double two_pi = 2.0 * std::numbers::pi;
    Rng rng(seed);
    const int m = spec.sinusoids;
    const double wd = two_pi * doppler;
    const double theta = rng.uniform(-std::numbers::pi, std::numbers::pi);
    branches_.reserve(static_cast<size_t>(m));
    for (int n = 1; n <= m; ++n) {
        const double alpha = (two_pi * n - std::numbers::pi + theta) / (4.0 * m);
        Branch b;
        b.w_i = wd * std::cos(alpha);
        b.phi_i = rng.uniform(-std::numbers::pi, std::numbers::pi);
        b.w_q = wd * std::sin(alpha);
        b.phi_q = rng.uniform(-std::numbers::pi, std::numbers::pi);
        branches_.push_back(b);
    }
    scatter_amp_ = 1.0 / std::sqrt(static_cast<double>(m));
    if (spec.type == FadingType::rician) {
        const double k = std::pow(10.0, spec.k_db / 10.0);
        los_amp_ = std::sqrt(k / (k + 1.0));
        scatter_amp_ *= std::sqrt(1.0 / (k + 1.0));
        const double aoa = rng.uniform(-std::numbers::pi, std::numbers::pi);
        los_w_ = wd * std::cos(aoa);
        los_phase_ = rng.uniform(-std::numbers::pi, std::numbers::pi);
    }
}

std::complex<double> FadingGenerator::gain(double t) const
{
    if (spec_.type == FadingType::none)
        return {1.0, 0.0};
    double re = 0.0, im = 0.0;
    for (const auto& b : branches_) {
        re += std::cos(b.w_i * t + b.phi_i);
        im += std::cos(b.w_q * t + b.phi_q);
    }
    std::complex<double> g(re * scatter_amp_, im * scatter_amp_);
    if (los_amp_ > 0.0)
        g += std::polar(los_amp_, los_w_ * t + los_phase_);
    return g;
}

// ----------------------------------------------------------- multipath --

Propagation propagation(const std::string& name)
{
    std::string n = name;
    for (char& c : n)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (n == "static")
        return {"static", {{0.0, 0.0}}, 0.0, false};
    if (n == "tu50")
        return {"TU50", {{0.0, 0.0}, {5.0, -22.3}}, 50.0, true};
    if (n == "ht200")
        return {"HT200", {{0.0, 0.0}, {15.0, -8.6}}, 200.0, true};
    throw std::invalid_argument("unknown propagation condition \"" + name + "\" (static, TU50, HT200)");
}

std::vector<std::string> propagation_names() { return {"static", "TU50", "HT200"}; }

MultipathChannel::MultipathChannel(const Propagation& p, double rate_hz, double carrier_hz, uint64_t seed)
    : rate_(rate_hz)
{
    if (!(rate_hz > 0.0) || !std::isfinite(rate_hz) || p.taps.empty() || !(carrier_hz >= 0.0))
        throw std::invalid_argument("multipath channel: rate must be > 0 and the profile needs taps");
    identity_ = !p.fading && p.taps.size() == 1 && p.taps[0].delay_us == 0.0;
    if (identity_)
        return;
    double total = 0.0;
    double max_delay = 0.0;
    for (const Tap& t : p.taps) {
        if (!(t.delay_us >= 0.0) || !std::isfinite(t.power_db))
            throw std::invalid_argument("multipath channel: tap delays must be >= 0");
        total += std::pow(10.0, t.power_db / 10.0);
        max_delay = std::max(max_delay, t.delay_us * 1e-6 * rate_hz);
    }
    if (max_delay > 4096.0)
        throw std::invalid_argument("multipath channel: tap delay beyond 4096 samples");
    length_ = static_cast<size_t>(2 * kHalfLength + 1 + static_cast<int>(std::ceil(max_delay)));
    doppler_ = p.speed_kmh / 3.6 / kSpeedOfLight * carrier_hz;
    FadingSpec spec;
    spec.type = p.fading ? FadingType::rayleigh : FadingType::none;
    for (size_t k = 0; k < p.taps.size(); ++k) {
        const double d = p.taps[k].delay_us * 1e-6 * rate_hz;
        const double amp = std::sqrt(std::pow(10.0, p.taps[k].power_db / 10.0) / total);
        Path path{std::vector<float>(length_, 0.0f), FadingGenerator(spec, doppler_, derive_seed(seed, {0x7A9, k})),
                  {1.0, 0.0}};
        // Windowed sinc centred at kHalfLength + d: y[n] = sum h[m] x[n - m].
        for (size_t m = 0; m < length_; ++m) {
            const double x = static_cast<double>(m) - kHalfLength - d;
            if (std::abs(x) > kHalfLength)
                continue;
            const double sinc = x == 0.0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
            const double a = std::numbers::pi * (x / (kHalfLength + 1) + 1.0);  // 0..2 pi over the window
            const double w = 0.42 - 0.5 * std::cos(a) + 0.08 * std::cos(2.0 * a);
            path.h[m] = static_cast<float>(amp * sinc * w);
        }
        paths_.push_back(std::move(path));
    }
    hist_.assign(length_ - 1, std::complex<float>(0.0f, 0.0f));
}

void MultipathChannel::apply(std::complex<float>* x, size_t n)
{
    if (identity_)
        return;
    const size_t keep = length_ - 1;
    hist_.resize(keep + n);
    std::copy(x, x + n, hist_.begin() + static_cast<std::ptrdiff_t>(keep));
    for (size_t i = 0; i < n; ++i, ++n_) {
        if (n_ % kGainStep == 0)
            for (Path& p : paths_)
                p.gain = p.fade.gain(static_cast<double>(n_) / rate_);
        // hist_[keep + i] is x[n]; x[n - m] is hist_[keep + i - m].
        const std::complex<float>* newest = hist_.data() + keep + i;
        std::complex<double> y(0.0, 0.0);
        for (const Path& p : paths_) {
            std::complex<double> acc(0.0, 0.0);
            for (size_t m = 0; m < length_; ++m)
                if (p.h[m] != 0.0f)
                    acc += static_cast<double>(p.h[m]) *
                           std::complex<double>(newest[-static_cast<std::ptrdiff_t>(m)]);
            y += acc * p.gain;
        }
        x[i] = std::complex<float>(y);
    }
    std::copy(hist_.end() - static_cast<std::ptrdiff_t>(keep), hist_.end(), hist_.begin());
    hist_.resize(keep);
}

// ------------------------------------------------- frequency and noise --

double thermal_noise_dbm(double bandwidth_hz, double nf_db)
{
    if (!(bandwidth_hz > 0.0))
        throw std::invalid_argument("noise bandwidth must be > 0");
    return kThermalNoiseDbmPerHz + 10.0 * std::log10(bandwidth_hz) + nf_db;
}

double dbm_to_power(double dbm, double reference_dbm) { return std::pow(10.0, (dbm - reference_dbm) / 10.0); }

double power_to_dbm(double power, double reference_dbm) { return reference_dbm + 10.0 * std::log10(power); }

FrequencyShifter::FrequencyShifter(double frequency_hz, double sample_rate_hz, double phase_cycles)
    : step_(frequency_hz / sample_rate_hz), cycles_(phase_cycles)
{
    if (!(sample_rate_hz > 0.0) || !std::isfinite(step_))
        throw std::invalid_argument("frequency shifter: sample rate must be > 0");
}

void FrequencyShifter::apply(std::complex<float>* x, size_t n)
{
    const double two_pi = 2.0 * std::numbers::pi;
    for (size_t i = 0; i < n; ++i) {
        const double a = two_pi * cycles_;
        const std::complex<double> r(std::cos(a), std::sin(a));
        x[i] = std::complex<float>(std::complex<double>(x[i]) * r);
        cycles_ += step_;
        cycles_ -= std::floor(cycles_);
    }
}

Awgn::Awgn(double power, uint64_t seed) : power_(power), sigma_(std::sqrt(power / 2.0)), rng_(seed)
{
    if (!(power >= 0.0) || !std::isfinite(power))
        throw std::invalid_argument("noise power must be >= 0");
}

std::complex<double> Awgn::sample()
{
    const double re = rng_.gaussian();
    const double im = rng_.gaussian();
    return {sigma_ * re, sigma_ * im};
}

void Awgn::add(std::complex<float>* x, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        x[i] += std::complex<float>(sample());
}

// -------------------------------------------------- receiver front end --

FrontEnd::FrontEnd(const FrontEndSpec& spec) : spec_(spec)
{
    if (!std::isfinite(spec.gain_db) || !std::isfinite(spec.iq_gain_db) || !std::isfinite(spec.iq_phase_deg) ||
        !std::isfinite(spec.dc.real()) || !std::isfinite(spec.dc.imag()) || !std::isfinite(spec.full_scale) ||
        !(spec.full_scale > 0.0))
        throw std::invalid_argument("front end: values must be finite and full scale > 0");
    if (spec.quantise_bits != 0 && (spec.quantise_bits < 2 || spec.quantise_bits > 16))
        throw std::invalid_argument("front end: quantise_bits must be 0 or 2..16");
    set_gain_db(spec.gain_db);
    const double g = std::pow(10.0, spec.iq_gain_db / 20.0);
    const double phi = spec.iq_phase_deg * std::numbers::pi / 180.0;
    q_cos_ = g * std::cos(phi);
    q_sin_ = g * std::sin(phi);
    if (spec.quantise_bits > 0) {
        const double levels = std::ldexp(1.0, spec.quantise_bits - 1);
        step_ = spec.full_scale / levels;
        lo_ = -levels;
        hi_ = levels - 1.0;
    }
}

void FrontEnd::set_gain_db(double gain_db)
{
    if (!std::isfinite(gain_db))
        throw std::invalid_argument("front end: gain must be finite");
    spec_.gain_db = gain_db;
    gain_ = std::pow(10.0, gain_db / 20.0);
}

double FrontEnd::image_rejection_db() const
{
    // y = mu x + nu conj(x): mu = (1 + g e^{-j phi}) / 2, nu = (1 - g e^{j phi}) / 2.
    const std::complex<double> ge(q_cos_, q_sin_);
    const double mu = std::norm((1.0 + std::conj(ge)) / 2.0);
    const double nu = std::norm((1.0 - ge) / 2.0);
    if (nu <= 0.0)
        return std::numeric_limits<double>::infinity();
    return 10.0 * std::log10(mu / nu);
}

void FrontEnd::apply(std::complex<float>* x, size_t n)
{
    const bool imbalance = spec_.iq_gain_db != 0.0 || spec_.iq_phase_deg != 0.0;
    const double fs = spec_.full_scale;
    for (size_t k = 0; k < n; ++k) {
        double i = static_cast<double>(x[k].real()) * gain_;
        double q = static_cast<double>(x[k].imag()) * gain_;
        if (imbalance) {
            const double q2 = q_cos_ * q - q_sin_ * i;
            q = q2;
        }
        if (spec_.swap_iq)
            std::swap(i, q);
        i += spec_.dc.real();
        q += spec_.dc.imag();
        bool at_limit = false;
        if (spec_.clip) {
            if (i >= fs || i <= -fs || q >= fs || q <= -fs)
                at_limit = true;
            i = std::clamp(i, -fs, fs);
            q = std::clamp(q, -fs, fs);
        }
        if (spec_.quantise_bits > 0) {
            double qi = std::round(i / step_);
            double qq = std::round(q / step_);
            if (qi <= lo_ || qi >= hi_ || qq <= lo_ || qq >= hi_)
                at_limit = true;
            qi = std::clamp(qi, lo_, hi_);
            qq = std::clamp(qq, lo_, hi_);
            i = qi * step_;
            q = qq * step_;
        }
        if (at_limit)
            ++clipped_;
        x[k] = std::complex<float>(static_cast<float>(i), static_cast<float>(q));
    }
}

SampleDropper::SampleDropper(const DropSpec& spec) : spec_(spec)
{
    if (spec.interval > 0 && spec.length >= spec.interval)
        throw std::invalid_argument("sample drops: length must be below the interval");
}

bool SampleDropper::dropped(uint64_t index) const
{
    if (spec_.interval == 0 || spec_.length == 0 || index < spec_.first)
        return false;
    return (index - spec_.first) % spec_.interval < spec_.length;
}

uint64_t SampleDropper::dropped_before(uint64_t index) const
{
    if (spec_.interval == 0 || spec_.length == 0 || index <= spec_.first)
        return 0;
    const uint64_t m = index - spec_.first;
    return (m / spec_.interval) * spec_.length + std::min(m % spec_.interval, spec_.length);
}

size_t SampleDropper::process(const std::complex<float>* in, size_t n, std::complex<float>* out)
{
    size_t m = 0;
    for (size_t k = 0; k < n; ++k, ++in_)
        if (!dropped(in_))
            out[m++] = in[k];
    return m;
}

}  // namespace yantra::gen
