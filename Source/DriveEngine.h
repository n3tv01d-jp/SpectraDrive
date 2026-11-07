// Copyright (C) 2026 Spectra
//
// This file is part of SpectraDrive, licensed under the GNU Affero General Public
// License v3.0 (AGPLv3). See the LICENSE file in the project root for details.

#pragma once

// SpectraDrive harmonic distortion engine.
//
// Signal flow (per channel):
//
//   in ─┬──────────────────────────────────────────────── dry (latency-aligned) ──┐
//       └─ crossover ─┬─ low band (kept clean, latency-aligned) ───────────┐      │
//                     └─ high band × drive + bias → [oversample → shaper   │      ├─ mix → out gain → out
//                          → downsample] − DC → tone tilt → auto gain ─────┴─ wet ┘
//
// Plain C++17 with no framework dependency so it can be unit-tested outside the plugin
// (same approach as SpectraShell's ResonatorEngine).
//
// Efficiency notes:
//  * Coefficient math (exp/pow/tan) runs once per 64-sample chunk; parameters are ramped
//    linearly inside the chunk, so automation never zippers.
//  * Oversampling uses linear-phase polyphase half-band FIRs (Kaiser-windowed sinc).
//    Only the odd taps are non-zero, so each stage costs K multiply-adds per sample.
//  * Latency is a whole number of samples in every quality mode, so the dry and clean-low
//    paths are aligned exactly and the host can compensate it.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace spectra
{
constexpr int kMaxChannels = 2;
constexpr int kChunk       = 64;     // control-rate granularity (samples)
constexpr int kNumModes    = 5;
constexpr int kNumQualities = 3;     // 1x, 2x, 4x

enum Mode : int { Tube = 0, Tape, Diode, Hard, Fold };

constexpr float kMaxDriveDb     = 40.0f;
constexpr float kCleanLowsOffHz = 20.0f;   // Clean Lows at (or below) this = off

// Half-band filter sizes. Each stage has 4K-1 taps of which K are distinct non-trivial values.
//  Stage A (1x <-> 2x): K=20, 79 taps, ~90 dB stopband, passband flat to ~0.43 fs.
//  Stage B (2x <-> 4x): K=6, 23 taps. Its input is already band-limited to a quarter of its
//  rate, so it only needs a wide transition band.
constexpr int kKA = 20;
constexpr int kKB = 6;
constexpr int kLatency2x = 2 * kKA - 1;                       // 39 samples
constexpr int kLatency4x = kLatency2x + ((2 * kKB - 1) + 1) / 2; // 39 + 6 (incl. 1-sample pad at 2x)

struct DriveParams
{
    int   mode        = Tube;
    int   quality     = 1;       // 0: 1x, 1: 2x, 2: 4x oversampling
    float driveDb     = 12.0f;   // 0 .. kMaxDriveDb
    float even        = 0.2f;    // 0..1, asymmetry -> even harmonics
    float cleanLowsHz = kCleanLowsOffHz;   // crossover; lows below stay clean
    float tone        = 0.0f;    // -1..1, post tilt (dark .. bright)
    bool  autoGain    = true;    // match wet loudness to the input
    float mix         = 1.0f;    // dry/wet
    float outGainDb   = 0.0f;
};

//==============================================================================
// Transfer functions. All have slope 1 at the origin (transparent for small signals)
// and are bounded, so the wet signal never runs away however hard it is driven.
inline float shape (int mode, float u) noexcept
{
    switch (mode)
    {
        case Tube:   // asymmetric: soft positive knee to +1, firmer negative knee to -0.8
            return u >= 0.0f ? 1.0f - std::exp (-u)
                             : (std::exp (1.25f * u) - 1.0f) * 0.8f;
        case Tape:   // symmetric soft saturation: odd harmonics
            return std::tanh (u);
        case Diode:  // sharper knee than tape
            return u / std::sqrt (std::sqrt (1.0f + u * u * u * u));
        case Hard:   // hard clip
            return std::clamp (u, -1.0f, 1.0f);
        case Fold:   // sine wavefolder
            return std::sin (u);
        default:
            return u;
    }
}

// DC bias added before the shaper; breaks symmetry and so adds even harmonics.
inline float biasFor (float even) noexcept { return 0.9f * std::clamp (even, 0.0f, 1.0f); }

// Static curve as heard: drive, bias, then the bias offset removed. Used by the UI too.
inline float transfer (int mode, float x, float driveGain, float bias) noexcept
{
    return shape (mode, x * driveGain + bias) - shape (mode, bias);
}

inline int latencyFor (int quality) noexcept
{
    return quality == 1 ? kLatency2x : quality == 2 ? kLatency4x : 0;
}

//==============================================================================
namespace detail
{
    inline double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k)
        {
            const double t = x / (2.0 * k);
            term *= t * t;
            sum += term;
            if (term < 1e-14 * sum) break;
        }
        return sum;
    }

    // Non-zero side taps h_j at offsets ±(2j+1) of a Kaiser-windowed half-band lowpass
    // (centre tap is 0.5). Normalised for exactly unity gain at DC.
    template <int K>
    std::array<float, (size_t) K> designHalfband (double beta)
    {
        constexpr double pi = 3.14159265358979323846;
        const double half = 2.0 * K;   // window half-length (end taps stay non-zero)
        std::array<double, (size_t) K> h {};
        double sum = 0.0;
        for (int j = 0; j < K; ++j)
        {
            const double d = 2.0 * j + 1.0;
            const double r = d / half;
            const double w = besselI0 (beta * std::sqrt (1.0 - r * r)) / besselI0 (beta);
            h[(size_t) j] = std::sin (pi * d * 0.5) / (pi * d) * w;
            sum += h[(size_t) j];
        }
        std::array<float, (size_t) K> out {};
        for (int j = 0; j < K; ++j)
            out[(size_t) j] = (float) (h[(size_t) j] * 0.25 / sum);
        return out;
    }

    // 1 -> 2 polyphase interpolator. Delay: 2K-1 samples at the high rate.
    template <int K>
    struct HalfbandUp
    {
        static constexpr int L = 2 * K;
        float buf[(size_t) (2 * L)] {};   // mirrored ring buffer: x[n-k] == buf[pos + k]
        int pos = 0;

        void reset() noexcept { std::fill (std::begin (buf), std::end (buf), 0.0f); pos = 0; }

        inline void process (const float* h, float x, float& y0, float& y1) noexcept
        {
            pos = (pos == 0 ? L : pos) - 1;
            buf[pos] = buf[pos + L] = x;
            const float* b = buf + pos;
            float acc = 0.0f;
            for (int j = 0; j < K; ++j)
                acc += h[j] * (b[K - 1 - j] + b[K + j]);
            y0 = 2.0f * acc;
            y1 = b[K - 1];
        }
    };

    // 2 -> 1 polyphase decimator. Delay: 2K-1 samples at the high rate.
    template <int K>
    struct HalfbandDown
    {
        static constexpr int L  = 2 * K;   // even-phase history
        static constexpr int OL = K + 1;   // odd-phase history
        float e[(size_t) (2 * L)] {};
        float o[(size_t) (2 * OL)] {};
        int epos = 0, opos = 0;

        void reset() noexcept
        {
            std::fill (std::begin (e), std::end (e), 0.0f);
            std::fill (std::begin (o), std::end (o), 0.0f);
            epos = opos = 0;
        }

        inline float process (const float* h, float v0, float v1) noexcept
        {
            epos = (epos == 0 ? L : epos) - 1;
            e[epos] = e[epos + L] = v0;
            opos = (opos == 0 ? OL : opos) - 1;
            o[opos] = o[opos + OL] = v1;

            const float* eb = e + epos;
            float acc = 0.0f;
            for (int j = 0; j < K; ++j)
                acc += h[j] * (eb[K + j] + eb[K - 1 - j]);
            return acc + 0.5f * o[opos + K];
        }
    };
}

//==============================================================================
class DriveEngine
{
public:
    DriveEngine()
        : hA (detail::designHalfband<kKA> (9.0)),
          hB (detail::designHalfband<kKB> (8.0))
    {
    }

    void prepare (double sampleRate, int numChannels)
    {
        fs = (float) sampleRate;
        channels = std::clamp (numChannels, 1, kMaxChannels);
        activeQuality = std::clamp (params.quality, 0, kNumQualities - 1);
        reset();
    }

    void reset() noexcept
    {
        for (auto& ch : chans)
            ch = Channel {};
        const DriveParams& p = params;
        smDrive  = dbToGain (std::clamp (p.driveDb, 0.0f, kMaxDriveDb));
        smBias   = biasFor (p.even);
        smXover  = std::max (p.cleanLowsHz, 10.0f);
        smLowG   = tiltLow (p.tone);
        smHighG  = tiltHigh (p.tone);
        smMix    = std::clamp (p.mix, 0.0f, 1.0f);
        smOut    = dbToGain (p.outGainDb);
        smAg     = 1.0f;
        agHeld   = 1.0f;
        msIn = msOut = 0.0f;
        inState = outState = driveState = 0.0f;
        inMeter.store (0.0f);   outMeter.store (0.0f);
        driveMeter.store (0.0f); agMeter.store (1.0f);
    }

    void setParams (const DriveParams& p) noexcept { params = p; }

    void process (float* const* io, int numChannels, int numSamples) noexcept
    {
        const int nCh = std::min ({ numChannels, channels, kMaxChannels });
        if (nCh <= 0)
            return;

        const int q = std::clamp (params.quality, 0, kNumQualities - 1);
        if (q != activeQuality)
        {
            activeQuality = q;
            for (auto& ch : chans)
            {
                ch.upA.reset(); ch.downA.reset(); ch.upB.reset(); ch.downB.reset();
                ch.pad = 0.0f;
            }
        }

        float inPk = 0.0f, outPk = 0.0f, drvPk = 0.0f;
        for (int pos = 0; pos < numSamples;)
        {
            const int len = std::min (kChunk, numSamples - pos);
            switch (activeQuality)
            {
                case 0:  processChunk<0> (io, nCh, pos, len, inPk, outPk, drvPk); break;
                case 1:  processChunk<1> (io, nCh, pos, len, inPk, outPk, drvPk); break;
                default: processChunk<2> (io, nCh, pos, len, inPk, outPk, drvPk); break;
            }
            pos += len;
        }

        // Meters for the UI: peak hold with ~300 ms fall.
        const float fall = std::exp (-(float) numSamples / (0.3f * fs));
        inState    = std::max (inPk,  inState * fall);
        outState   = std::max (outPk, outState * fall);
        driveState = std::max (drvPk, driveState * fall);
        inMeter.store (inState, std::memory_order_relaxed);
        outMeter.store (outState, std::memory_order_relaxed);
        driveMeter.store (driveState, std::memory_order_relaxed);
        agMeter.store (smAg, std::memory_order_relaxed);
    }

    int   getLatencySamples() const noexcept { return latencyFor (activeQuality); }
    float getInputLevel() const noexcept     { return inMeter.load (std::memory_order_relaxed); }
    float getOutputLevel() const noexcept    { return outMeter.load (std::memory_order_relaxed); }
    float getDriveLevel() const noexcept     { return driveMeter.load (std::memory_order_relaxed); }  // peak |input × drive|
    float getAutoGain() const noexcept       { return agMeter.load (std::memory_order_relaxed); }

private:
    static constexpr int kDelayLen  = 128;   // > kLatency4x, power of two
    static constexpr int kDelayMask = kDelayLen - 1;

    struct Svf { float ic1 = 0.0f, ic2 = 0.0f; };   // TPT state-variable filter state

    struct Channel
    {
        Svf xo1, xoLow, xoHigh;                       // Linkwitz-Riley 4 crossover
        detail::HalfbandUp<kKA>   upA;
        detail::HalfbandDown<kKA> downA;
        detail::HalfbandUp<kKB>   upB;
        detail::HalfbandDown<kKB> downB;
        float pad = 0.0f;                             // 1-sample delay at 2x (4x mode)
        float dcX = 0.0f, dcY = 0.0f;                 // DC blocker
        float tilt = 0.0f;                            // tone tilt one-pole state
        float dryDly[kDelayLen] {};
        float lowDly[kDelayLen] {};
        int   dw = 0;
    };

    static float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
    static float tiltHigh (float t) noexcept  { return dbToGain ( 6.0f * std::clamp (t, -1.0f, 1.0f)); }
    static float tiltLow (float t) noexcept   { return dbToGain (-6.0f * std::clamp (t, -1.0f, 1.0f)); }

    template <int Q>
    inline float overShape (Channel& ch, int mode, float u) noexcept
    {
        if constexpr (Q == 0)
        {
            return shape (mode, u);
        }
        else if constexpr (Q == 1)
        {
            float v0, v1;
            ch.upA.process (hA.data(), u, v0, v1);
            return ch.downA.process (hA.data(), shape (mode, v0), shape (mode, v1));
        }
        else
        {
            float v0, v1;
            ch.upA.process (hA.data(), u, v0, v1);
            const float p0 = ch.pad;   // pad keeps the total latency a whole base-rate sample
            ch.pad = v1;
            float w0, w1, w2, w3;
            ch.upB.process (hB.data(), p0, w0, w1);
            ch.upB.process (hB.data(), v0, w2, w3);
            const float r0 = ch.downB.process (hB.data(), shape (mode, w0), shape (mode, w1));
            const float r1 = ch.downB.process (hB.data(), shape (mode, w2), shape (mode, w3));
            return ch.downA.process (hA.data(), r0, r1);
        }
    }

    template <int Q>
    void processChunk (float* const* io, int nCh, int offset, int len,
                       float& inPk, float& outPk, float& drvPk) noexcept
    {
        const DriveParams& p = params;
        const int   mode     = std::clamp (p.mode, 0, kNumModes - 1);
        const int   lat      = latencyFor (Q);
        const float chunkSec = (float) len / fs;
        const float inv      = 1.0f / (float) len;
        const float aSlow    = 1.0f - std::exp (-chunkSec / 0.020f);   // parameter glide
        const float aRms     = 1.0f - std::exp (-chunkSec / 0.300f);   // loudness tracking
        const float aAg      = 1.0f - std::exp (-chunkSec / 0.150f);   // auto gain glide

        // --- Ramped parameters ---------------------------------------------------
        const float g0 = smDrive;
        smDrive += (dbToGain (std::clamp (p.driveDb, 0.0f, kMaxDriveDb)) - smDrive) * aSlow;
        const float gStep = (smDrive - g0) * inv;

        const float b0 = smBias;
        smBias += (biasFor (p.even) - smBias) * aSlow;
        const float bStep = (smBias - b0) * inv;
        const float sb0 = shape (mode, b0);
        const float sbStep = (shape (mode, smBias) - sb0) * inv;

        const float lg0 = smLowG, hg0 = smHighG;
        smLowG  += (tiltLow (p.tone)  - smLowG)  * aSlow;
        smHighG += (tiltHigh (p.tone) - smHighG) * aSlow;
        const float lgStep = (smLowG - lg0) * inv, hgStep = (smHighG - hg0) * inv;

        // Crossover (TPT SVF, Butterworth). LP² + HP² of the same filter sums to an allpass.
        smXover += (std::clamp (p.cleanLowsHz, 10.0f, 2000.0f) - smXover) * aSlow;
        const bool  split = p.cleanLowsHz > kCleanLowsOffHz + 0.5f;
        const float xg  = std::tan (3.14159265f * std::min (smXover, 0.45f * fs) / fs);
        const float xk  = 1.41421356f;
        const float xa1 = 1.0f / (1.0f + xg * (xg + xk));
        const float xa2 = xg * xa1;
        const float xa3 = xg * xa2;

        // Tone tilt pivot (one-pole TPT lowpass at 1 kHz) and DC blocker (~5 Hz).
        const float tgRaw = std::tan (3.14159265f * 1000.0f / fs);
        const float tG    = tgRaw / (1.0f + tgRaw);
        const float dcR   = std::exp (-6.2831853f * 5.0f / fs);

        double eIn = 0.0, eOut = 0.0;

        for (int c = 0; c < nCh; ++c)
        {
            Channel& ch = chans[(size_t) c];
            const float* in = io[c] + offset;

            for (int i = 0; i < len; ++i)
            {
                const float fi = (float) i;
                const float x  = in[i];
                inPk = std::max (inPk, std::fabs (x));

                // --- Crossover (always running so switching it on is seamless)
                float lo, hi;
                {
                    auto svf = [&] (Svf& s, float v, float& lp, float& hp)
                    {
                        const float v3 = v - s.ic2;
                        const float v1 = xa1 * s.ic1 + xa2 * v3;
                        const float v2 = s.ic2 + xa2 * s.ic1 + xa3 * v3;
                        s.ic1 = 2.0f * v1 - s.ic1;
                        s.ic2 = 2.0f * v2 - s.ic2;
                        lp = v2;
                        hp = v - xk * v1 - v2;
                    };
                    float lp1, hp1, lp2, hp2, unused;
                    svf (ch.xo1, x, lp1, hp1);
                    svf (ch.xoLow, lp1, lp2, unused);
                    svf (ch.xoHigh, hp1, unused, hp2);
                    lo = split ? lp2 : 0.0f;
                    hi = split ? hp2 : x;
                }
                eIn += (double) hi * hi;

                // --- Drive + shaper
                const float driven = hi * (g0 + gStep * fi);
                drvPk = std::max (drvPk, std::fabs (driven));
                float s = overShape<Q> (ch, mode, driven + b0 + bStep * fi) - (sb0 + sbStep * fi);

                // --- DC blocker
                const float y = s - ch.dcX + dcR * ch.dcY;
                ch.dcX = s;
                ch.dcY = (std::fabs (y) < 1e-20f) ? 0.0f : y;

                // --- Tone tilt
                const float tv = (y - ch.tilt) * tG;
                const float tl = tv + ch.tilt;
                ch.tilt = tl + tv;
                const float t = tl * (lg0 + lgStep * fi) + (y - tl) * (hg0 + hgStep * fi);
                eOut += (double) t * t;
                wet[c][i] = t;

                // --- Latency-aligned dry and clean-low paths
                ch.dryDly[ch.dw] = x;
                ch.lowDly[ch.dw] = lo;
                const int r = (ch.dw - lat) & kDelayMask;
                dry[c][i]  = ch.dryDly[r];
                lowD[c][i] = ch.lowDly[r];
                ch.dw = (ch.dw + 1) & kDelayMask;
            }
        }

        // --- Auto gain: stereo-linked loudness match of the shaped band to its input.
        const float n = (float) (len * nCh);
        msIn  += ((float) eIn  / n - msIn)  * aRms;
        msOut += ((float) eOut / n - msOut) * aRms;
        if (msIn > 1e-10f)   // hold through silence
            agHeld = std::clamp (std::sqrt (msIn / (msOut + 1e-20f)), 0.0625f, 2.0f);
        const float agTarget = p.autoGain ? agHeld : 1.0f;
        const float ag0 = smAg;
        smAg += (agTarget - smAg) * aAg;
        const float agStep = (smAg - ag0) * inv;

        const float m0 = smMix;
        smMix += (std::clamp (p.mix, 0.0f, 1.0f) - smMix) * aSlow;
        const float mStep = (smMix - m0) * inv;
        const float o0 = smOut;
        smOut += (dbToGain (p.outGainDb) - smOut) * aSlow;
        const float oStep = (smOut - o0) * inv;

        for (int c = 0; c < nCh; ++c)
        {
            float* o = io[c] + offset;
            for (int i = 0; i < len; ++i)
            {
                const float fi = (float) i;
                const float mx = m0 + mStep * fi;
                const float w  = wet[c][i] * (ag0 + agStep * fi) + lowD[c][i];
                const float v  = (dry[c][i] * (1.0f - mx) + w * mx) * (o0 + oStep * fi);
                o[i] = v;
                outPk = std::max (outPk, std::fabs (v));
            }
        }
    }

    DriveParams params;
    float fs = 48000.0f;
    int channels = 2;
    int activeQuality = 1;

    const std::array<float, kKA> hA;
    const std::array<float, kKB> hB;
    std::array<Channel, kMaxChannels> chans {};

    float wet[kMaxChannels][kChunk] {};
    float dry[kMaxChannels][kChunk] {};
    float lowD[kMaxChannels][kChunk] {};

    float smDrive = 1.0f, smBias = 0.0f, smXover = 20.0f, smLowG = 1.0f, smHighG = 1.0f;
    float smMix = 1.0f, smOut = 1.0f, smAg = 1.0f, agHeld = 1.0f;
    float msIn = 0.0f, msOut = 0.0f;

    float inState = 0.0f, outState = 0.0f, driveState = 0.0f;
    std::atomic<float> inMeter { 0.0f }, outMeter { 0.0f }, driveMeter { 0.0f }, agMeter { 1.0f };
};

} // namespace spectra
