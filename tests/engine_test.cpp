// Copyright (C) 2026 Spectra
//
// This file is part of SpectraDrive, licensed under the GNU Affero General Public
// License v3.0 (AGPLv3). See the LICENSE file in the project root for details.

// Standalone checks for the drive engine: transparency, latency, harmonic character,
// aliasing, stability, auto gain and CPU cost. Build with -DSPECTRADRIVE_BUILD_TESTS=ON
// -DSPECTRADRIVE_PLUGIN=OFF.

#include "DriveEngine.h"

#include <chrono>
#include <complex>
#include <cstdio>
#include <random>
#include <vector>

using spectra::DriveEngine;
using spectra::DriveParams;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static const float fs = 48000.0f;
static const char* const kModes[] = { "Tube", "Tape", "Diode", "Hard", "Fold" };

static float toDb (double v) { return (float) (10.0 * std::log10 (v + 1e-30)); }   // v is power

// Runs the engine on a mono signal duplicated to stereo; returns the left channel.
static std::vector<float> run (DriveEngine& e, const std::vector<float>& in, int block = 512)
{
    std::vector<float> L (in), R (in);
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - pos);
        float* ch[2] = { L.data() + pos, R.data() + pos };
        e.process (ch, 2, n);
    }
    for (float v : L) if (! std::isfinite (v)) { ++failures; std::printf ("FAIL: non-finite output\n"); break; }
    return L;
}

static double rms (const std::vector<float>& v, size_t from, size_t to)
{
    double s = 0; for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / (double) (to - from));
}

// In-place radix-2 FFT.
static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

constexpr size_t kN = 65536;

// Power spectrum (bins 0..N/2) of N samples starting at `from`. The test tones sit exactly
// on bins and are periodic within the window, so no window function is needed.
static std::vector<double> spectrum (const std::vector<float>& v, size_t from)
{
    std::vector<std::complex<double>> a (kN);
    for (size_t i = 0; i < kN; ++i) a[i] = v[from + i];
    fft (a);
    std::vector<double> p (kN / 2 + 1);
    for (size_t i = 0; i <= kN / 2; ++i) p[i] = std::norm (a[i]) / ((double) kN * kN);
    return p;
}

static std::vector<float> sine (size_t bin, float amp, size_t len)
{
    std::vector<float> s (len);
    for (size_t i = 0; i < len; ++i)
        s[i] = amp * (float) std::sin (2.0 * 3.14159265358979323846 * (double) bin * (double) i / (double) kN);
    return s;
}

struct Harmonics { double h[11] {}; double alias = 0; };   // h[1..10], alias = everything else

static Harmonics analyse (const std::vector<double>& p, size_t f0)
{
    Harmonics r;
    std::vector<bool> isHarm (p.size(), false);
    for (size_t h = 1; h * f0 <= kN / 2; ++h)
    {
        isHarm[h * f0] = true;
        if (h <= 10) r.h[h] = p[h * f0];
    }
    for (size_t i = 20; i < p.size(); ++i)   // skip DC / sub-sonic region
        if (! isHarm[i]) r.alias += p[i];
    return r;
}

int main()
{
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);

    // 1. Latency: an impulse through a clean (drive 0 dB, tiny level) wet path and the dry
    //    path must arrive exactly `latency` samples later, in every quality mode.
    for (int q = 0; q < 3; ++q)
    {
        for (float mix : { 0.0f, 1.0f })
        {
            DriveEngine e; DriveParams p; p.quality = q; p.driveDb = 0.0f; p.even = 0.0f; p.mode = spectra::Tape;
            p.mix = mix; p.autoGain = false; p.tone = 0.0f;
            e.setParams (p); e.prepare (fs, 2);
            std::vector<float> in (4096, 0.0f);
            in[1000] = 1e-3f;
            auto out = run (e, in);
            size_t peakAt = 0;
            for (size_t i = 0; i < out.size(); ++i) if (std::fabs (out[i]) > std::fabs (out[peakAt])) peakAt = i;
            const int lat = (int) peakAt - 1000;
            std::printf ("latency q=%d mix=%.0f: measured %d, reported %d\n", q, mix, lat, e.getLatencySamples());
            CHECK (lat == e.getLatencySamples(), "latency mismatch q=%d", q);
        }
    }

    // 2. Transparency: at drive 0 dB a quiet signal passes unchanged (apart from the delay and the
    //    DC blocker's tiny phase shift).
    for (int q = 0; q < 3; ++q)
    {
        DriveEngine e; DriveParams p; p.quality = q; p.driveDb = 0.0f; p.even = 0.0f; p.mode = spectra::Tape;
        p.mix = 1.0f; p.autoGain = false;
        e.setParams (p); e.prepare (fs, 2);
        auto in = sine (1365, 0.01f, kN + 8192);   // ~1 kHz
        auto out = run (e, in);
        const int lat = e.getLatencySamples();
        double err = 0, ref = 0;
        for (size_t i = 8192; i < out.size(); ++i)
        {
            const double d = out[i] - in[i - (size_t) lat];
            err += d * d; ref += (double) in[i] * in[i];
        }
        const float errDb = toDb (err / ref);
        std::printf ("transparency q=%d: error %.1f dB\n", q, errDb);
        CHECK (errDb < -40.0f, "not transparent q=%d (%.1f dB)", q, errDb);
    }

    // 3. Harmonic character: odd vs even harmonics per mode and Even knob.
    {
        struct Case { int mode; float even; bool expectEven; };
        const Case cases[] = {
            { spectra::Tube,  0.0f, true  },
            { spectra::Tape,  0.0f, false },
            { spectra::Tape,  0.6f, true  },
            { spectra::Diode, 0.0f, false },
            { spectra::Hard,  0.0f, false },
            { spectra::Fold,  0.0f, false },
            { spectra::Fold,  0.5f, true  },
        };
        const size_t f0 = 1365;   // ~1 kHz
        for (const auto& cs : cases)
        {
            DriveEngine e; DriveParams p; p.mode = cs.mode; p.even = cs.even; p.driveDb = 12.0f;
            p.quality = 2; p.mix = 1.0f; p.autoGain = false;
            e.setParams (p); e.prepare (fs, 2);
            auto out = run (e, sine (f0, 0.5f, kN + 16384));
            auto h = analyse (spectrum (out, 16384), f0);
            const float h2 = toDb (h.h[2] / h.h[1]), h3 = toDb (h.h[3] / h.h[1]);
            double odd = 0, even = 0;
            for (int k = 2; k <= 10; ++k) (k % 2 ? odd : even) += h.h[k];
            const float thd = 100.0f * (float) std::sqrt ((odd + even) / h.h[1]);
            std::printf ("harmonics %-5s even=%.1f: H2 %6.1f dB  H3 %6.1f dB  THD %5.1f %%\n",
                         kModes[cs.mode], cs.even, h2, h3, thd);
            if (cs.expectEven) CHECK (h2 > -40.0f, "%s should have even harmonics", kModes[cs.mode]);
            else               CHECK (h2 < -90.0f, "%s should be odd-only (H2 %.1f dB)", kModes[cs.mode], h2);
            CHECK (std::max (h2, h3) > -40.0f, "%s produced no distortion", kModes[cs.mode]);
        }
    }

    // 4. Aliasing: a 7 kHz tone hard-driven. Everything that isn't a true harmonic below
    //    Nyquist is aliasing; oversampling must push it down substantially.
    for (int mode : { (int) spectra::Tape, (int) spectra::Hard })
    {
        const size_t f0 = 9557;   // 6999.5 Hz
        float aliasDb[3] {};
        for (int q = 0; q < 3; ++q)
        {
            DriveEngine e; DriveParams p; p.mode = mode; p.driveDb = 18.0f; p.even = 0.0f;
            p.quality = q; p.mix = 1.0f; p.autoGain = false;
            e.setParams (p); e.prepare (fs, 2);
            auto out = run (e, sine (f0, 0.5f, kN + 16384));
            auto h = analyse (spectrum (out, 16384), f0);
            double total = 0; for (int k = 1; k <= 3; ++k) total += h.h[k];
            aliasDb[q] = toDb (h.alias / total);
        }
        std::printf ("aliasing %-5s 7 kHz +18 dB: 1x %.1f dB  2x %.1f dB  4x %.1f dB\n",
                     kModes[mode], aliasDb[0], aliasDb[1], aliasDb[2]);
        CHECK (aliasDb[1] < aliasDb[0] - 10.0f, "2x did not reduce aliasing");
        CHECK (aliasDb[2] < aliasDb[1] - 6.0f,  "4x did not reduce aliasing");
        if (mode == spectra::Tape) CHECK (aliasDb[2] < -70.0f, "tape 4x aliasing too high");
    }

    // 5. Stability: every mode / quality, maximum drive and bias, loud noise + fast automation.
    {
        float worst = 0;
        for (int mode = 0; mode < spectra::kNumModes; ++mode)
        for (int q = 0; q < 3; ++q)
        {
            DriveEngine e; e.prepare (fs, 2);
            std::vector<float> in ((size_t) fs);
            for (auto& v : in) v = uni (rng) * 2.0f;
            std::vector<float> L (in), R (in);
            for (size_t pos = 0, k = 0; pos < in.size(); pos += 100, ++k)
            {
                DriveParams p; p.mode = mode; p.quality = q;
                p.driveDb = (k & 1) ? spectra::kMaxDriveDb : 0.0f;
                p.even = (k & 2) ? 1.0f : 0.0f; p.tone = (k & 4) ? 1.0f : -1.0f;
                p.cleanLowsHz = (k & 8) ? 800.0f : 20.0f; p.autoGain = true;
                e.setParams (p);
                const int n = (int) std::min ((size_t) 100, in.size() - pos);
                float* ch[2] = { L.data() + pos, R.data() + pos };
                e.process (ch, 2, n);
            }
            for (float v : L)
            {
                if (! std::isfinite (v)) { ++failures; std::printf ("FAIL: non-finite (mode %d q %d)\n", mode, q); break; }
                worst = std::max (worst, std::fabs (v));
            }
        }
        std::printf ("stress: worst peak %.2f\n", worst);
        CHECK (worst < 12.0f, "stress peak too high %.2f", worst);
    }

    // 6. Auto gain: driving 30 dB harder should not make it much louder.
    for (int mode = 0; mode < spectra::kNumModes; ++mode)
    {
        DriveEngine e; DriveParams p; p.mode = mode; p.driveDb = 30.0f; p.autoGain = true; p.mix = 1.0f;
        e.setParams (p); e.prepare (fs, 2);
        std::vector<float> in ((size_t) (fs * 3));
        for (auto& v : in) v = uni (rng) * 0.1f;
        auto out = run (e, in);
        const float d = 20.0f * std::log10 ((float) (rms (out, out.size() / 2, out.size()) / rms (in, in.size() / 2, in.size())));
        std::printf ("auto gain %-5s +30 dB drive: output %+.1f dB vs input\n", kModes[mode], d);
        CHECK (std::fabs (d) < 3.0f, "auto gain off by %.1f dB", d);
    }

    // 7. Clean Lows: a 50 Hz tone below a 250 Hz crossover stays clean at high drive.
    {
        const size_t f0 = 68;   // ~49.8 Hz
        float thd[2] {};
        for (int on = 0; on < 2; ++on)
        {
            DriveEngine e; DriveParams p; p.mode = spectra::Tape; p.driveDb = 24.0f; p.even = 0.0f;
            p.cleanLowsHz = on ? 250.0f : 20.0f; p.autoGain = false; p.mix = 1.0f;
            e.setParams (p); e.prepare (fs, 2);
            auto out = run (e, sine (f0, 0.5f, kN + 16384));
            auto h = analyse (spectrum (out, 16384), f0);
            double hs = 0; for (int k = 2; k <= 10; ++k) hs += h.h[k];
            thd[on] = 100.0f * (float) std::sqrt (hs / h.h[1]);
        }
        std::printf ("clean lows: 50 Hz THD %.1f %% (off) -> %.2f %% (250 Hz)\n", thd[0], thd[1]);
        CHECK (thd[1] < thd[0] * 0.1f, "clean lows did not keep the low end clean");
    }

    // 8. CPU: stereo, 10 s of audio, each quality.
    {
        std::vector<float> in ((size_t) (fs * 10));
        for (auto& v : in) v = uni (rng) * 0.3f;
        for (int q = 0; q < 3; ++q)
        {
            DriveEngine e; DriveParams p; p.quality = q; p.mode = spectra::Tube; p.cleanLowsHz = 120.0f;
            e.setParams (p); e.prepare (fs, 2);
            const auto t0 = std::chrono::steady_clock::now();
            run (e, in, 256);
            const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            std::printf ("cpu: %dx stereo, 10 s audio in %.3f s (%.2f%% of one core)\n", 1 << q, sec, sec / 10.0 * 100.0);
        }
    }

    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
