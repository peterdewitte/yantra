// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SIMD kernels with GCC/Clang vector extensions: 4-float vectors map to
// one SSE2 register on x86-64 and one NEON register on aarch64 (Linux
// arm64, RAD-131), with no intrinsics and no run-time dispatch. Loads and
// stores go through memcpy, so no alignment is assumed.

#include "kernels_internal.hpp"

#include <cstring>

namespace yantra::receiver {
namespace {

typedef float v4 __attribute__((vector_size(16)));

inline v4 load(const float* p)
{
    v4 v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

inline void store(float* p, v4 v) { std::memcpy(p, &v, sizeof v); }

inline const float* fl(const cf* p) { return reinterpret_cast<const float*>(p); }
inline float* fl(cf* p) { return reinterpret_cast<float*>(p); }

cf dot_simd(const cf* x, const float* h2, std::size_t n)
{
    const float* xf = fl(x);
    v4 a0 = {0, 0, 0, 0};
    v4 a1 = a0;
    v4 a2 = a0;
    v4 a3 = a0;
    std::size_t k = 0;
    // 8 complex samples (16 floats) per round, 4 independent sums.
    for (; k + 8 <= n; k += 8) {
        const float* xp = xf + 2 * k;
        const float* hp = h2 + 2 * k;
        a0 += load(xp) * load(hp);
        a1 += load(xp + 4) * load(hp + 4);
        a2 += load(xp + 8) * load(hp + 8);
        a3 += load(xp + 12) * load(hp + 12);
    }
    for (; k + 2 <= n; k += 2)
        a0 += load(xf + 2 * k) * load(h2 + 2 * k);
    const v4 s = (a0 + a1) + (a2 + a3);
    float re = s[0] + s[2];
    float im = s[1] + s[3];
    for (; k < n; ++k) {
        re += x[k].real() * h2[2 * k];
        im += x[k].imag() * h2[2 * k + 1];
    }
    return {re, im};
}

cf rotate_simd(const cf* in, cf* out, std::size_t n, cf phasor, cf step)
{
    if (n < 8) {
        for (std::size_t k = 0; k < n; ++k) {
            out[k] = in[k] * phasor;
            phasor *= step;
        }
        return phasor;
    }
    const cf s2 = step * step;
    const cf s4 = s2 * s2;
    const cf p1 = phasor * step;
    const cf p2 = phasor * s2;
    const cf p3 = p1 * s2;
    v4 pr = {phasor.real(), p1.real(), p2.real(), p3.real()};
    v4 pi = {phasor.imag(), p1.imag(), p2.imag(), p3.imag()};
    const v4 sr = {s4.real(), s4.real(), s4.real(), s4.real()};
    const v4 si = {s4.imag(), s4.imag(), s4.imag(), s4.imag()};
    std::size_t k = 0;
    for (; k + 4 <= n; k += 4) {
        const v4 a = load(fl(in + k));
        const v4 b = load(fl(in + k + 2));
        const v4 xr = __builtin_shufflevector(a, b, 0, 2, 4, 6);
        const v4 xi = __builtin_shufflevector(a, b, 1, 3, 5, 7);
        const v4 yr = xr * pr - xi * pi;
        const v4 yi = xr * pi + xi * pr;
        store(fl(out + k), __builtin_shufflevector(yr, yi, 0, 4, 1, 5));
        store(fl(out + k + 2), __builtin_shufflevector(yr, yi, 2, 6, 3, 7));
        const v4 nr = pr * sr - pi * si;
        pi = pr * si + pi * sr;
        pr = nr;
    }
    phasor = {pr[0], pi[0]};
    for (; k < n; ++k) {
        out[k] = in[k] * phasor;
        phasor *= step;
    }
    return phasor;
}

void accumulate_power_simd(const cf* x, float* acc, std::size_t n)
{
    std::size_t k = 0;
    for (; k + 4 <= n; k += 4) {
        const v4 a = load(fl(x + k));
        const v4 b = load(fl(x + k + 2));
        const v4 sa = a * a;
        const v4 sb = b * b;
        const v4 p = __builtin_shufflevector(sa, sb, 0, 2, 4, 6) + __builtin_shufflevector(sa, sb, 1, 3, 5, 7);
        store(acc + k, load(acc + k) + p);
    }
    for (; k < n; ++k)
        acc[k] += x[k].real() * x[k].real() + x[k].imag() * x[k].imag();
}

void apply_window_simd(const cf* x, const float* w, cf* out, std::size_t n)
{
    std::size_t k = 0;
    for (; k + 4 <= n; k += 4) {
        const v4 wv = load(w + k);
        const v4 a = load(fl(x + k));
        const v4 b = load(fl(x + k + 2));
        store(fl(out + k), a * __builtin_shufflevector(wv, wv, 0, 0, 1, 1));
        store(fl(out + k + 2), b * __builtin_shufflevector(wv, wv, 2, 2, 3, 3));
    }
    for (; k < n; ++k)
        out[k] = x[k] * w[k];
}

}  // namespace

namespace detail {
const Kernels kSimdKernels{Isa::Simd, "simd", dot_simd, rotate_simd, accumulate_power_simd, apply_window_simd};
}  // namespace detail

}  // namespace yantra::receiver
