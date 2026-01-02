#pragma once
#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>
#include <memory>

namespace compass
{
class DSPCore final
{
public:
    DSPCore() = default;


    // Forward declaration: Phase 4 members reference Biquad before its definition.
    private:
    struct Biquad;
    public:

    // ===== Phase 4: Oversampling scaffold members (LMF only) =====
    std::unique_ptr<juce::dsp::Oversampling<float>> osLmf;
    // ===== Phase 4 (LMF only): oversampling wiring support (members only; no process wiring in Step 4C) =====
    static constexpr int kLmfXfadeSamples = 64;

    std::vector<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>> lmfDryAlign;
    float lmfOsLatencySamples = 0.0f;
    std::vector<float> lmfXfade01;
    std::vector<int>   lmfXfadePos;
    bool  lmfOsEngaged    = false;
    bool  lmfXfadeActive  = false;
    float lmfXfadeStepPerSample = 0.0f;


    
    // ===== Phase 4F-B0/4F-B: LMF island buffers (prepare-only; NO allocations in process) =====
    int lmfMaxBlock = 0;

    std::vector<std::vector<float>> lmfPreBuf;        // pre-LMF (after LF shelf), [ch][i]
    std::vector<std::vector<float>> lmfPostDryBuf;    // post-LMF dry (base-rate), [ch][i]
    std::vector<std::vector<float>> lmfPostOsBuf;     // post-LMF OS (computed), [ch][i]
    std::vector<float> outGCache;                     // per-sample out trim cache for Pass C (size = lmfMaxBlock)

    // Pointer arrays for JUCE AudioBlock construction (prepare-sized; assigned per block; NO alloc in process)
    std::vector<const float*> lmfInPtrs;
    std::vector<float*>       lmfOsPtrs;

    // OS-rate LMF peak biquad state (separate from base-rate lmfPeak)
    std::vector<Biquad> lmfPeakOs;
    // Step 4D: engage decision uses existing Phase 3 threshold behavior (no new constants)
    inline bool shouldEngageLmfOs() const noexcept
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"

        // Use the same gain signal path Phase 3 uses (user gain -> cut restore -> boost protect).
        // Engage OS iff Phase 3 would modify the gain due to extreme boost/cut.
        const float gUser = sanitizeDb ((float) lmfGainDbSm.getCurrentValue());
        const float gCut  = phase3RestoreCutDb (gUser);
        const float gProt = phase3ProtectBoostDb (gCut);
        auto neq = [](float a, float b) noexcept
        {
            return std::abs(a - b) > 1.0e-6f;
        };
        return neq(gCut, gUser) || neq(gProt, gCut);
    
#pragma clang diagnostic pop
}

    size_t osLmfChannels = 0;



    // ===== Phase 3: Pure Mode bridge (internal; not a parameter) =====
    // ===== Phase 4: Oversampling scaffold (LMF only; infrastructure only) =====
    // Allocation is forbidden in DSPCore::prepare/process. Call this from a known
    // non-audio-thread lifecycle path (e.g., AudioProcessor::prepareToPlay).
    inline void initOversampling (int numChannels)
    {
        const size_t ch = (size_t) juce::jmax (1, numChannels);
        if (osLmf != nullptr && osLmfChannels == ch)
            return;

        // Allocate outside DSPCore::prepare/process (Phase 4 rule).
        osLmf = std::make_unique<juce::dsp::Oversampling<float>>(
            ch,
            (size_t) 1, // 2^1 = 2x (wiring happens in Step 4)
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
            true,  // max quality
            true   // use integer latency (alignment wired later)
        );
        osLmfChannels = ch;
    }

    inline void setPureMode (bool enabled) noexcept { pureMode = enabled; }
    inline bool getPureMode() const noexcept        { return pureMode; }
    inline void prepare (double sampleRate, int samplesPerBlock, int numChannels)
    {
        pureMode = false; // Phase 3C: lifecycle safety (prepare)
        sr = (sampleRate > 0.0 ? sampleRate : 44100.0);
        channels = juce::jmax (1, numChannels);

        // per-channel 1st-order HPF state
        hp1_x1.assign ((size_t) channels, 0.0f);
        hp1_y1.assign ((size_t) channels, 0.0f);

        // per-channel biquad states (HPF2, LF shelf, LMF peak, HMF peak, HF shelf, LPF2)
        hpf2.assign    ((size_t) channels, Biquad{});
        lfShelf.assign ((size_t) channels, Biquad{});
        lmfPeak.assign ((size_t) channels, Biquad{});
        hmfPeak.assign ((size_t) channels, Biquad{});
        // Phase 3R scaffold: per-channel state (LMF/HMF only; wired later)
        phase3rLmf.assign ((size_t) channels, Phase3RBandState{});
        phase3rHmf.assign ((size_t) channels, Phase3RBandState{});
        hfShelf.assign ((size_t) channels, Biquad{});
        lpf2.assign    ((size_t) channels, Biquad{});

        constexpr double smoothTimeSec = 0.02; // 20 ms

        // trims + filters
        inTrimLin.reset  (sr, smoothTimeSec);
        outTrimLin.reset (sr, smoothTimeSec);
        hpfHzSm.reset    (sr, smoothTimeSec);
        lpfHzSm.reset    (sr, smoothTimeSec);

        // bands
        lfFreqSm.reset    (sr, smoothTimeSec);
        lfGainDbSm.reset  (sr, smoothTimeSec);

        lmfFreqSm.reset   (sr, smoothTimeSec);
        lmfGainDbSm.reset (sr, smoothTimeSec);
        lmfQSm.reset      (sr, smoothTimeSec);

        hmfFreqSm.reset   (sr, smoothTimeSec);
        hmfGainDbSm.reset (sr, smoothTimeSec);
        hmfQSm.reset      (sr, smoothTimeSec);

        hfFreqSm.reset    (sr, smoothTimeSec);
        hfGainDbSm.reset  (sr, smoothTimeSec);

        // safe initial values (owned upstream; we just initialize)
        inTrimLin.setCurrentAndTargetValue  (1.0f);
        outTrimLin.setCurrentAndTargetValue (1.0f);

        hpfHzSm.setCurrentAndTargetValue (20.0f);
        lpfHzSm.setCurrentAndTargetValue (20000.0f);

        lfFreqSm.setCurrentAndTargetValue    (100.0f);
        lfGainDbSm.setCurrentAndTargetValue  (0.0f);

        lmfFreqSm.setCurrentAndTargetValue   (1000.0f);
        lmfGainDbSm.setCurrentAndTargetValue (0.0f);
        lmfQSm.setCurrentAndTargetValue      (1.0f);

        hmfFreqSm.setCurrentAndTargetValue   (3000.0f);
        hmfGainDbSm.setCurrentAndTargetValue (0.0f);
        hmfQSm.setCurrentAndTargetValue      (1.0f);

        hfFreqSm.setCurrentAndTargetValue    (8000.0f);
        hfGainDbSm.setCurrentAndTargetValue  (0.0f);

        invalidateAllLastValues();

        // build initial coefficients (NO audio-thread allocations; pure math)
        updateFirstOrderHPF (hpfHzSm.getCurrentValue());
        rebuildAllBiquads();

        // Phase 4 Step 3 scaffold: no allocation; only init/reset on pre-created OS
        if (osLmf != nullptr)
        {
            osLmf->initProcessing ((size_t) juce::jmax (1, samplesPerBlock));
            // Step 4B guardrail: only read latency if osLmf exists
            lmfOsLatencySamples = (float) osLmf->getLatencyInSamples();
            osLmf->reset();
        }
        else
        {
            lmfOsLatencySamples = 0.0f;
        }

        // Step 4C: allocate/resize LMF-only alignment + xfade state in prepare() (no process wiring yet)
        lmfDryAlign.resize ((size_t) channels);
        lmfXfade01.assign ((size_t) channels, lmfOsEngaged ? 1.0f : 0.0f);
        lmfXfadePos.assign ((size_t) channels, lmfOsEngaged ? 63 : 0);
        lmfXfadeActive = false;
        lmfXfadeStepPerSample = 1.0f / 64.0f;

        juce::dsp::ProcessSpec drySpec;
        drySpec.sampleRate = (double) sr;
        drySpec.maximumBlockSize = (juce::uint32) juce::jmax (1, samplesPerBlock);
        drySpec.numChannels = 1;
        for (auto& d : lmfDryAlign)
        {
            d.prepare (drySpec);
            d.setDelay (lmfOsLatencySamples);
            d.reset();
        }

        
        // ===== Phase 4F-B0/4F-B: allocate LMF island buffers (prepare-only) =====
        lmfMaxBlock = juce::jmax(1, samplesPerBlock);

        lmfPreBuf.resize((size_t) channels);
        lmfPostDryBuf.resize((size_t) channels);
        lmfPostOsBuf.resize((size_t) channels);

        for (int ch = 0; ch < channels; ++ch)
        {
            lmfPreBuf[(size_t) ch].assign((size_t) lmfMaxBlock, 0.0f);
            lmfPostDryBuf[(size_t) ch].assign((size_t) lmfMaxBlock, 0.0f);
            lmfPostOsBuf[(size_t) ch].assign((size_t) lmfMaxBlock, 0.0f);
        }

        outGCache.assign((size_t) lmfMaxBlock, 1.0f);

        lmfInPtrs.assign((size_t) channels, nullptr);
        lmfOsPtrs.assign((size_t) channels, nullptr);

        lmfPeakOs.assign((size_t) channels, Biquad{});
reset();
    }

    inline void reset() noexcept
    {
        pureMode = false; // Phase 3C: lifecycle safety (reset)
        // Step 4C: reset LMF-only alignment + stable xfade state (no ramping)
        for (auto& d : lmfDryAlign)
            d.reset();
        for (auto& b : lmfPeakOs) b.reset();

        // Zero LMF island buffers + cache (no allocations)
        for (auto& v : lmfPreBuf)      for (auto& s : v) s = 0.0f;
        for (auto& v : lmfPostDryBuf)  for (auto& s : v) s = 0.0f;
        for (auto& v : lmfPostOsBuf)   for (auto& s : v) s = 0.0f;
        for (auto& g : outGCache)      g = 1.0f;
        for (auto& v : lmfXfade01)
            v = (lmfOsEngaged ? 1.0f : 0.0f);
        for (auto& p : lmfXfadePos)
            p = (lmfOsEngaged ? 63 : 0);
        lmfXfadeActive = false;

        // Phase 4 Step 3 scaffold: OS state reset only (no processing use yet)
        if (osLmf != nullptr)
            osLmf->reset();
        for (auto& b : hpf2)    b.reset();
        for (auto& b : lfShelf) b.reset();
        for (auto& b : lmfPeak) b.reset();
        for (auto& b : hmfPeak) b.reset();
        for (auto& s : phase3rLmf) { s = Phase3RBandState{}; }
        for (auto& s : phase3rHmf) { s = Phase3RBandState{}; }
        for (auto& b : hfShelf) b.reset();
        for (auto& b : lpf2)    b.reset();

        for (size_t ch = 0; ch < hp1_x1.size(); ++ch)
        {
            hp1_x1[ch] = 0.0f;
            hp1_y1[ch] = 0.0f;
        }
    }

    // Phase 2A targets (trims + HPF/LPF)
    inline void setTargets (float inTrimDb, float outTrimDb, float hpfHz, float lpfHz) noexcept
    {
        inTrimLin.setTargetValue  (juce::Decibels::decibelsToGain (inTrimDb));
        outTrimLin.setTargetValue (juce::Decibels::decibelsToGain (outTrimDb));
        hpfHzSm.setTargetValue (sanitizeHz (hpfHz));
        lpfHzSm.setTargetValue (sanitizeHz (lpfHz));
    }

    // Phase 2B targets (EQ bands)
    inline void setBandTargets (
        float lfFreqHz, float lfGainDb,
        float lmfFreqHz, float lmfGainDb, float lmfQ,
        float hmfFreqHz, float hmfGainDb, float hmfQ,
        float hfFreqHz, float hfGainDb) noexcept
    {
        lfFreqSm.setTargetValue    (sanitizeHz (lfFreqHz));
        lfGainDbSm.setTargetValue  (sanitizeDb (lfGainDb));

        lmfFreqSm.setTargetValue   (sanitizeHz (lmfFreqHz));
        lmfGainDbSm.setTargetValue (sanitizeDb (lmfGainDb));
        lmfQSm.setTargetValue      (sanitizeQ  (lmfQ));

        hmfFreqSm.setTargetValue   (sanitizeHz (hmfFreqHz));
        hmfGainDbSm.setTargetValue (sanitizeDb (hmfGainDb));
        hmfQSm.setTargetValue      (sanitizeQ  (hmfQ));

        hfFreqSm.setTargetValue    (sanitizeHz (hfFreqHz));
        hfGainDbSm.setTargetValue  (sanitizeDb (hfGainDb));
    }

    inline void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n   = buffer.getNumSamples();
        const int chs = juce::jmin (channels, buffer.getNumChannels());

        // Pure Mode (Phase 3A): trims only — skip smoothers + filters/EQ entirely
        if (pureMode)
        {
            for (int i = 0; i < n; ++i)
            {
                const float inG  = inTrimLin.getNextValue();
                const float outG = outTrimLin.getNextValue();

                const float g = inG * outG;
                for (int ch = 0; ch < chs; ++ch)
                {
                    auto* d = buffer.getWritePointer (ch);
                    d[i] *= g;
                }
            }
            return;
        }

        // ===== Phase 4E (LMF OS): engage decision + start/stop crossfade flag (NO audio-path wiring yet) =====
        const bool wantOs = (osLmf != nullptr) && shouldEngageLmfOs();
        if (wantOs != lmfOsEngaged)
        {
            lmfOsEngaged   = wantOs;
            lmfXfadeActive = true;

            // No allocations here. Containers must already be sized in prepare().
            const int nCh = juce::jmax (1, chs);
            if ((int) lmfXfadePos.size() >= nCh && (int) lmfXfade01.size() >= nCh)
            {
                for (int ch = 0; ch < nCh; ++ch)
                {
                    lmfXfadePos[(size_t) ch] = wantOs ? 0 : (kLmfXfadeSamples - 1);
                    lmfXfade01[(size_t) ch]  = wantOs ? 0.0f : 1.0f;
                }
            }
            else
            {
                // Safety: if state wasn't prepared, do not attempt a transition.
                lmfXfadeActive = false;
            }
        }
        // ===== End Phase 4E (LMF OS) =====
        // ===== Phase 4F-B0 Pass A: pre-LMF capture (cadence preserved; the ONLY place getNextValue + updateFiltersIfNeeded(i) runs) =====
        for (int i = 0; i < n; ++i)
        {
            const float inG  = inTrimLin.getNextValue();
            const float outG = outTrimLin.getNextValue();
            if ((size_t) i < outGCache.size())
                outGCache[(size_t) i] = outG;

            // advance smoothers (filters)
            hpfHzSm.getNextValue();
            lpfHzSm.getNextValue();

            // advance smoothers (bands)
            lfFreqSm.getNextValue();
            lfGainDbSm.getNextValue();

            lmfFreqSm.getNextValue();
            lmfGainDbSm.getNextValue();
            lmfQSm.getNextValue();

            hmfFreqSm.getNextValue();
            hmfGainDbSm.getNextValue();
            hmfQSm.getNextValue();

            hfFreqSm.getNextValue();
            hfGainDbSm.getNextValue();

            updateFiltersIfNeeded (i); // cadence preserved (exactly once per i)

            for (int ch = 0; ch < chs; ++ch)
            {
                float x = buffer.getSample (ch, i);

                // Input Trim (applied exactly once here)
                x *= inG;

                // HPF 18 dB/oct = 12 dB biquad + 6 dB first-order
                if (hpfActive)
                {
                    x = hpf2[(size_t) ch].process (x);
                    x = processFirstOrderHPF (ch, x);
                }

                // EQ Bands: stop at pre-LMF
                x = lfShelf[(size_t) ch].process (x);

                // Store PRE-LMF
                if ((size_t) ch < lmfPreBuf.size() && (size_t) i < lmfPreBuf[(size_t) ch].size())
                    lmfPreBuf[(size_t) ch][(size_t) i] = x;
            }
        }

        // ===== Phase 4F-B: Pass B LMF island (dry + OS chunk; NO allocations) =====
        const bool lmfIslandOk =
            (lmfMaxBlock >= n) &&
            ((int) lmfPreBuf.size() >= chs) &&
            ((int) lmfPostDryBuf.size() >= chs) &&
            ((int) lmfPostOsBuf.size() >= chs) &&
            ((int) outGCache.size() >= n) &&
            ((int) lmfInPtrs.size() >= chs) &&
            ((int) lmfOsPtrs.size() >= chs) &&
            ((int) lmfPeakOs.size() >= chs);

        bool computeOs = lmfIslandOk && (osLmf != nullptr) && (osLmfChannels == (size_t) chs) && (lmfOsEngaged || lmfXfadeActive);

        for (int ch = 0; ch < chs; ++ch)
        {
            for (int i = 0; i < n; ++i)
            {
                float x = ((size_t) ch < lmfPreBuf.size() && (size_t) i < lmfPreBuf[(size_t) ch].size())
                            ? lmfPreBuf[(size_t) ch][(size_t) i]
                            : 0.0f;

                x = lmfPeak[(size_t) ch].process (x);

                if ((size_t) ch < lmfPostDryBuf.size() && (size_t) i < lmfPostDryBuf[(size_t) ch].size())
                    lmfPostDryBuf[(size_t) ch][(size_t) i] = x;
            }
        }

        if (computeOs && n > 0)
        {
            const float lmfHz = sanitizeHz ((float) lmfFreqSm.getCurrentValue());
            const float lmfQ  = juce::jmax (0.05f, (float) lmfQSm.getCurrentValue());
            const float lmfDb = sanitizeDb ((float) lmfGainDbSm.getCurrentValue());
            const float osSr  = (float) (sr * 2.0);

            for (int ch = 0; ch < chs; ++ch)
            {
                lmfInPtrs[(size_t) ch] = lmfPreBuf[(size_t) ch].data();
                lmfOsPtrs[(size_t) ch] = lmfPostOsBuf[(size_t) ch].data();
                setPeakEqCoeffsOsRate(lmfPeakOs[(size_t) ch], osSr, lmfHz, lmfQ, lmfDb);
            }

            juce::dsp::AudioBlock<const float> inBlock(lmfInPtrs.data(), (size_t) chs, (size_t) n);
            juce::dsp::AudioBlock<float> outBlock(lmfOsPtrs.data(), (size_t) chs, (size_t) n);

            auto upBlock = osLmf->processSamplesUp(inBlock);
            const int upN = (int) upBlock.getNumSamples();

            for (int ch = 0; ch < chs; ++ch)
            {
                auto* up = upBlock.getChannelPointer((size_t) ch);
                for (int k = 0; k < upN; ++k)
                    up[k] = lmfPeakOs[(size_t) ch].process(up[k]);
            }

            osLmf->processSamplesDown(outBlock);
        }
        else
{
    for (int ch = 0; ch < chs; ++ch)
    {
        // Phase 6: hoist bounds checks / indexing out of the inner sample loop (behavior-identical)
        const size_t chZ = (size_t) ch;

        const float* drySrc = nullptr;
        size_t dryN = 0;
        if (chZ < lmfPostDryBuf.size())
        {
            const auto& dry = lmfPostDryBuf[chZ];
            drySrc = dry.data();
            dryN = dry.size();
        }

        float* osDst = nullptr;
        size_t osN = 0;
        if (chZ < lmfPostOsBuf.size())
        {
            auto& osb = lmfPostOsBuf[chZ];
            osDst = osb.data();
            osN = osb.size();
        }

        for (int i = 0; i < n; ++i)
        {
            const size_t iZ = (size_t) i;

            const float x = (iZ < dryN && drySrc != nullptr) ? drySrc[iZ] : 0.0f;

            if (iZ < osN && osDst != nullptr)
                osDst[iZ] = x;
        }
    }
}

        // ===== Phase 4F-B0 Pass C: post-LMF continuation + 4F-A state/select (NO lmfPeak.process here) =====
        for (int i = 0; i < n; ++i)
        {
            const float outG = ((size_t) i < outGCache.size() ? outGCache[(size_t) i] : 1.0f);

            for (int ch = 0; ch < chs; ++ch)
            {
                const float lmfDryOut =
                    ((size_t) ch < lmfPostDryBuf.size() && (size_t) i < lmfPostDryBuf[(size_t) ch].size())
                        ? lmfPostDryBuf[(size_t) ch][(size_t) i]
                        : 0.0f;

                const float lmfOsReal =
                    ((size_t) ch < lmfPostOsBuf.size() && (size_t) i < lmfPostOsBuf[(size_t) ch].size())
                        ? lmfPostOsBuf[(size_t) ch][(size_t) i]
                        : lmfDryOut;
                (void) lmfOsReal;

                float x = 0.0f;

                // ===== Phase 4F-A (LMF OS): ramp + dry-align + blend scaffold (STATE/SELECT ONLY) =====
                if ((size_t) ch < lmfDryAlign.size())
                    lmfDryAlign[(size_t) ch].pushSample (0, lmfDryOut);

                float dryAlignedLMF = lmfDryOut;
                if ((size_t) ch < lmfDryAlign.size())
                    dryAlignedLMF = lmfDryAlign[(size_t) ch].popSample (0);
                const float lmfOsOut = (lmfOsEngaged ? lmfOsReal : lmfDryOut);
                float lmfX = 0.0f;

                if ((size_t) ch < lmfXfadePos.size())
                {
                    const int last = (kLmfXfadeSamples - 1);
                    int p = lmfXfadePos[(size_t) ch];

                    if (lmfXfadeActive)
                    {
                        if (lmfOsEngaged) { if (p < last) ++p; }
                        else              { if (p > 0) --p; }
                        lmfXfadePos[(size_t) ch] = p;
                    }

                    lmfX = (last > 0) ? ((float) p / (float) last) : 0.0f;
                    if ((size_t) ch < lmfXfade01.size())
                        lmfXfade01[(size_t) ch] = lmfX;

                    if (lmfXfadeActive && (ch == (chs - 1)))
                    {
                        bool allDone = true;
                        const int nCh = juce::jmax (1, chs);
                        if ((int) lmfXfadePos.size() >= nCh)
                        {
                            const int wantTerminal = (lmfOsEngaged ? last : 0);
                            for (int c = 0; c < nCh; ++c)
                            {
                                if (lmfXfadePos[(size_t) c] != wantTerminal) { allDone = false; break; }
                            }
                            if (allDone)
                            {
                                lmfXfadeActive = false;
                                if ((int) lmfXfade01.size() >= nCh)
                                {
                                    const float xTerm = (lmfOsEngaged ? 1.0f : 0.0f);
                                    for (int c = 0; c < nCh; ++c)
                                        lmfXfade01[(size_t) c] = xTerm;
                                }
                            }
                        }
                    }
                }

                if (lmfXfadeActive) x = dryAlignedLMF + (lmfOsOut - dryAlignedLMF) * lmfX;
                else               x = lmfOsOut;
                // ===== End Phase 4F-A (LMF OS) =====

                if (phase3rLmfGateOpen)
                {
                    const float envA = phase3rEnvA_fast;
                    const float envB = (1.0f - envA);
                    x = phase3rProcessSample (phase3rLmf[(size_t) ch], x,
                                              true,
                                              envA, envB,
                                              phase3rPersistDecay,
                                              phase3rAtkA, phase3rRelA,
                                              phase3rDepthSlewPerSample);
                }

                x = hmfPeak[(size_t) ch].process (x);

                if (phase3rHmfGateOpen)
                {
                    const float envA = phase3rEnvA_fast;
                    const float envB = (1.0f - envA);
                    x = phase3rProcessSample (phase3rHmf[(size_t) ch], x,
                                              true,
                                              envA, envB,
                                              phase3rPersistDecay,
                                              phase3rAtkA, phase3rRelA,
                                              phase3rDepthSlewPerSample);
                }

                x = hfShelf[(size_t) ch].process (x);

                if (lpfActive)
                    x = lpf2[(size_t) ch].process (x);

                x *= outG;

                buffer.setSample (ch, i, x);
            }
        }

    }

private:
    // ---------- RT-safe biquad ----------
    struct Biquad
    {
        // normalized coefficients (a0 = 1)
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;

        // DF2T state
        float z1 = 0.0f, z2 = 0.0f;

        inline void reset() noexcept { z1 = 0.0f; z2 = 0.0f; }

        inline float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };


    // ===== Phase 4F-B: OS-rate biquad coefficient helper (LMF peak only) =====
    // Used ONLY for lmfPeakOs (oversampled-rate processing). Base-rate path remains unchanged.
    static inline void setPeakEqCoeffsOsRate(Biquad& b, float sampleRate, float freqHz, float Q, float gainDb) noexcept
    {
        const float sr = (sampleRate > 1.0f ? sampleRate : 44100.0f);
        const float f  = juce::jlimit(10.0f, 0.49f * sr, freqHz);
        const float q  = juce::jmax(0.05f, Q);
        const float A  = std::pow(10.0f, gainDb / 40.0f);

        const float w0 = 2.0f * (float)juce::MathConstants<double>::pi * (f / sr);
        const float cw = std::cos(w0);
        const float sw = std::sin(w0);
        const float alpha = sw / (2.0f * q);

        const float b0 = 1.0f + alpha * A;
        const float b1 = -2.0f * cw;
        const float b2 = 1.0f - alpha * A;
        const float a0 = 1.0f + alpha / A;
        const float a1 = -2.0f * cw;
        const float a2 = 1.0f - alpha / A;

        const float invA0 = (a0 != 0.0f ? 1.0f / a0 : 1.0f);
        b.b0 = b0 * invA0;
        b.b1 = b1 * invA0;
        b.b2 = b2 * invA0;
        b.a1 = a1 * invA0;
        b.a2 = a2 * invA0;
    }

    // ===== Mini-Phase 3R: Band-Local Resonance Suppression (Scaffold Only; no audio-path wiring) =====
    // IMPORTANT (Contract):
    //  - LMF/HMF only (implementation later)
    //  - Boost-only gating (implementation later)
    //  - Pure Mode bypass is by construction (Pure branch returns before filters/EQ)
    //  - Patch 1 adds state + helper builders ONLY (no sample-loop processing changes)
    struct Phase3RBandState
    {
        // Detector biquads (time-domain IIR only)
        Biquad narrowBP;   // intended: RBJ bandpass @ center freq, Q ~ 30 (wired later)
        Biquad broadBP;    // intended: RBJ bandpass @ center freq, Q = userQ/2 (wired later)

        // Suppressor biquad (post-band peaking cut @ center freq, fixed Q=8; wired later)
        Biquad suppressPeak;

        // Envelopes / smoothing (wired later)
        float narrowEnv = 0.0f;     // EWMA(abs(y)) for narrowBP
        float broadEnv  = 0.0f;     // EWMA(x^2) for broadBP (RMS-style)
        float persist   = 0.0f;     // persistence integrator (~50ms requirement)
        float detectSm  = 0.0f;     // slow detection smoothing (atk ~100ms / rel ~1s)

        // Depth control (wired later)
        float depthDb     = 0.0f;   // current suppression depth (0..3 dB)
        float lastDepthDb = 0.0f;   // last coefficient-applied depth (for control-rate updates)

        // Param change tracking for soft-decay on jumps (wired later)
        float lastFreqHz  = -1.0f;
        float lastQ       = -1.0f;
        float lastGainDb  = 9999.0f;
    };

    // RBJ bandpass (constant skirt gain) builder; used later for narrow/broad detectors.
    inline void makeBandPass (Biquad& bq, float hz, float q) noexcept
    {
        const float f0 = sanitizeHz (hz);
        const float Q  = sanitizeQ  (q);

        const float w0 = 2.0f * juce::MathConstants<float>::pi * (f0 / (float) sr);
        const float cw = std::cos (w0);
        const float sw = std::sin (w0);
        const float alpha = sw / (2.0f * Q);

        // RBJ bandpass (constant skirt gain, peak gain = Q)
        float b0 =  sw * 0.5f;
        float b1 =  0.0f;
        float b2 = -sw * 0.5f;
        float a0 =  1.0f + alpha;
        float a1 = -2.0f * cw;
        float a2 =  1.0f - alpha;

        const float invA0 = 1.0f / a0;
        bq.b0 = b0 * invA0;
        bq.b1 = b1 * invA0;
        bq.b2 = b2 * invA0;
        bq.a1 = a1 * invA0;
        bq.a2 = a2 * invA0;
    }

    // Suppressor builder (peaking EQ). Wired later; Patch 1 only.
    inline void makePhase3RSuppressor (Biquad& bq, float hz, float depthDbNeg) noexcept
    {
        // depthDbNeg is expected <= 0 (negative gain); Q fixed at 8 per contract.
        makePeakingEQ (bq, hz, 8.0f, depthDbNeg);
    }

    
    // ===== Phase 3R runtime (Patch 2): time-domain detector + control-rate coefficient updates =====
    // Contract locks:
    //  - eps = 1e-12f
    //  - ratio uses RMS/RMS: narrowRms / (broadRms + eps)
    //  - minBroadRms guard required
    //  - suppressor is peaking cut biquad Q=8 (makePhase3RSuppressor)
    //  - depth bound <= 3 dB, depth slew <= 1 dB/s
    //  - micro-depth bypass < 0.02 dB
    //  - boost-only gate > +1 dB (effective gain)
    //  - Pure Mode unchanged (Pure branch returns before filters/EQ)
    inline float phase3rProcessSample (Phase3RBandState& s,
                                      float xBand,
                                      const bool gateOpen,
                                      const float envA,
                                      const float envB,
                                      const float persistDecay,
                                      const float atkA,
                                      const float relA,
                                      const float depthSlewPerSample) noexcept
    {
        // If gate is closed, we smoothly release toward no-op (no hard reset).
        // Detection can decay naturally; suppression target is forced toward 0 via depth slew.
        const float n = s.narrowBP.process (xBand);
        const float b = s.broadBP.process  (xBand);

        // Fast envelopes (energy-based for both; RMS computed via sqrt)
        s.narrowEnv = envA * s.narrowEnv + envB * (n * n);
        s.broadEnv  = envA * s.broadEnv  + envB * (b * b);

        constexpr float eps         = 1.0e-12f;
        constexpr float minBroadRms = 1.0e-6f;

        const float narrowRms = std::sqrt (juce::jmax (0.0f, s.narrowEnv));
        const float broadRms  = std::sqrt (juce::jmax (0.0f, s.broadEnv));

        float excess = 0.0f;
        if (gateOpen && broadRms >= minBroadRms)
        {
            const float ratio = narrowRms / (broadRms + eps);
            excess = juce::jmax (0.0f, ratio - 1.0f);
            // Clamp excess before persistence (contract)
            excess = juce::jlimit (0.0f, 6.0f, excess);
        }

        // Persistence (~50ms): hold peaks, decay otherwise
        s.persist = juce::jmax (excess, s.persist * persistDecay);

        // Slow AR smoothing (atk ~100ms / rel ~1s) on persistence
        const float target = s.persist;
        const float a = (target > s.detectSm) ? atkA : relA;
        s.detectSm = a * s.detectSm + (1.0f - a) * target;

        // Map detectSm -> depthTargetDb (0..3 dB)
        float depthTargetDb = juce::jlimit (0.0f, 3.0f, s.detectSm * 3.0f);

        // If gate closed, target becomes 0 (true no-op goal), but release is slewed.
        if (! gateOpen)
            depthTargetDb = 0.0f;

        // Depth slew <= 1 dB/s (contract)
        const float d = depthTargetDb - s.depthDb;
        const float step = juce::jlimit (-depthSlewPerSample, depthSlewPerSample, d);
        s.depthDb += step;

        // Micro-depth bypass (< 0.02 dB)
        if (s.depthDb < 0.02f)
            return xBand;

        // Apply suppressor biquad (coeffs updated at control-rate only)
        return s.suppressPeak.process (xBand);
    }

    inline bool phase3rParamJump10pct (float now, float last) noexcept
    {
        if (last <= 0.0f) return true;
        return std::fabs (now - last) > (0.10f * last);
    }

    inline bool phase3rGainJump (float nowDb, float lastDb) noexcept
    {
        // conservative: treat ~0.5 dB delta as a "meaningful" jump for coeff refresh
        return std::fabs (nowDb - lastDb) > 0.5f;
    }

    inline void phase3rUpdateCoeffsForBand (Phase3RBandState& s,
                                           float freqHz,
                                           float qEff,
                                           float gainEffDb,
                                           bool gateOpen,
                                           bool forceSuppressorOnly) noexcept
    {
        // Detector biquads update on meaningful param jumps (control-rate only)
        const bool freqJump = (s.lastFreqHz < 0.0f) ? true : phase3rParamJump10pct (freqHz, s.lastFreqHz);
        const bool qJump    = (s.lastQ < 0.0f)      ? true : phase3rParamJump10pct (qEff,   s.lastQ);
        const bool gJump    = (s.lastGainDb > 9000.0f) ? true : phase3rGainJump (gainEffDb, s.lastGainDb);

        if (! forceSuppressorOnly && (freqJump || qJump || gJump))
        {
            // narrow detector: fixed high-Q bandpass
            makeBandPass (s.narrowBP, freqHz, 30.0f);

            // broad detector: tied to boosted band width (Q/2)
            makeBandPass (s.broadBP,  freqHz, juce::jmax (0.10f, qEff * 0.5f));

            s.lastFreqHz = freqHz;
            s.lastQ      = qEff;
            s.lastGainDb = gainEffDb;

            // Soft-decay on big jumps (no hard reset)
            s.persist  *= 0.5f;
            s.detectSm *= 0.5f;
        }

        // Suppressor coeff update if applied depth changed meaningfully and gate is open.
        // NOTE: coefficients are refreshed at control-rate only; sample-loop uses existing bq.
        if (gateOpen)
        {
            if (std::fabs (s.depthDb - s.lastDepthDb) >= 0.02f)
            {
                const float depthDbNeg = -juce::jlimit (0.0f, 3.0f, s.depthDb);
                makePhase3RSuppressor (s.suppressPeak, freqHz, depthDbNeg);
                s.lastDepthDb = s.depthDb;
            }
        }
        else
        {
            // Gate closed: ensure lastDepthDb chases toward 0 smoothly at control-rate.
            // We do not rebuild coeffs if micro-bypassed; sample-loop will bypass anyway.
            if (s.lastDepthDb != 0.0f && s.depthDb < 0.02f)
                s.lastDepthDb = 0.0f;
        }
    }

    // ===== End Phase 3R scaffold =====

    // ---------- sanitize helpers (no “Phase law” embedded; just safety clamps) (no “Phase law” embedded; just safety clamps) ----------
    inline float sanitizeHz (float hz) const noexcept
    {
        const float minHz = 1.0f;
        const float maxHz = (float) (sr * 0.45);
        return juce::jlimit (minHz, maxHz, hz);
    }

    inline static float sanitizeQ (float q) noexcept
    {
        return juce::jlimit (0.05f, 10.0f, q);
    }

    inline static float sanitizeDb (float db) noexcept
    {
        return juce::jlimit (-48.0f, 48.0f, db);
    }

    inline static float dbToA (float db) noexcept
    {
        // A = sqrt(10^(dB/20)) = 10^(dB/40)
        return std::pow (10.0f, db / 40.0f);
    }

    // ===== Phase 3: Protective Engine (parameter-driven only; no dynamics) =====
    // Boost protection: clamp extreme boosts (monotonic, bounded, reversible)
    inline static float phase3ProtectBoostDb (float db) noexcept
    {
        constexpr float kBoostThreshDb = 12.0f;
        constexpr float kBoostMaxDb    = 12.0f; // hard clamp for Phase 3 safety
        if (db <= kBoostThreshDb)
            return db;
        return kBoostMaxDb;
    }

    // Cut restoration: reduce *extreme* cuts slightly (structural only; self-limited)
    inline static float phase3RestoreCutDb (float db) noexcept
    {
        constexpr float kCutThreshDb   = -12.0f;
        constexpr float kRestoreMaxDb  =  1.0f;  // at most +1 dB of restoration
        if (db >= kCutThreshDb)
            return db;

        // depth beyond threshold (positive number)
        const float depth = (kCutThreshDb - db);
        // scale 0..1 over 0..12 dB beyond threshold, then cap restore
        const float t = juce::jlimit (0.0f, 1.0f, depth / 12.0f);
        const float restore = kRestoreMaxDb * t;
        return db + restore;
    }

    // Q widening for boosted peaking bands: small, bounded, monotonic
    inline static float phase3WidenQForBoost (float q, float gainDbEff) noexcept
    {
        constexpr float kBoostThreshDb = 12.0f;
        constexpr float kMinQ          = 0.25f; // never get too wide
        if (gainDbEff <= kBoostThreshDb)
            return q;
        // widen by reducing Q toward kMinQ (very conservative because boost already clamped)
        return juce::jmax (kMinQ, q * 0.85f);
    }

    // ---------- RBJ coefficient builders (pure math; no allocation) ----------
    inline void makeLowPass (Biquad& bq, float hz, float q) noexcept
    {
        const float w0 = 2.0f * juce::MathConstants<float>::pi * hz / (float) sr;
        const float cw = std::cos (w0);
        const float sw = std::sin (w0);
        const float alpha = sw / (2.0f * q);

        float b0 =  (1.0f - cw) * 0.5f;
        float b1 =   1.0f - cw;
        float b2 =  (1.0f - cw) * 0.5f;
        float a0 =   1.0f + alpha;
        float a1 =  -2.0f * cw;
        float a2 =   1.0f - alpha;

        const float invA0 = 1.0f / a0;
        bq.b0 = b0 * invA0;
        bq.b1 = b1 * invA0;
        bq.b2 = b2 * invA0;
        bq.a1 = a1 * invA0;
        bq.a2 = a2 * invA0;
    }

    inline void makeHighPass (Biquad& bq, float hz, float q) noexcept
    {
        const float w0 = 2.0f * juce::MathConstants<float>::pi * hz / (float) sr;
        const float cw = std::cos (w0);
        const float sw = std::sin (w0);
        const float alpha = sw / (2.0f * q);

        float b0 =  (1.0f + cw) * 0.5f;
        float b1 = -(1.0f + cw);
        float b2 =  (1.0f + cw) * 0.5f;
        float a0 =   1.0f + alpha;
        float a1 =  -2.0f * cw;
        float a2 =   1.0f - alpha;

        const float invA0 = 1.0f / a0;
        bq.b0 = b0 * invA0;
        bq.b1 = b1 * invA0;
        bq.b2 = b2 * invA0;
        bq.a1 = a1 * invA0;
        bq.a2 = a2 * invA0;
    }

    inline void makePeakingEQ (Biquad& bq, float hz, float q, float gainDb) noexcept
    {
        const float A  = std::pow (10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * juce::MathConstants<float>::pi * hz / (float) sr;
        const float cw = std::cos (w0);
        const float sw = std::sin (w0);
        const float alpha = sw / (2.0f * q);

        float b0 = 1.0f + alpha * A;
        float b1 = -2.0f * cw;
        float b2 = 1.0f - alpha * A;
        float a0 = 1.0f + alpha / A;
        float a1 = -2.0f * cw;
        float a2 = 1.0f - alpha / A;

        const float invA0 = 1.0f / a0;
        bq.b0 = b0 * invA0;
        bq.b1 = b1 * invA0;
        bq.b2 = b2 * invA0;
        bq.a1 = a1 * invA0;
        bq.a2 = a2 * invA0;
    }

    inline void makeLowShelf (Biquad& bq, float hz, float gainDb) noexcept
    {
        // RBJ shelf with slope S=1 (fixed topology; no extra params)
        const float A  = dbToA (gainDb);
        const float w0 = 2.0f * juce::MathConstants<float>::pi * hz / (float) sr;
        const float cw = std::cos (w0);
        const float sw = std::sin (w0);

        const float S = 1.0f;
        const float alpha = (sw / 2.0f) * std::sqrt ((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);
        const float beta  = 2.0f * std::sqrt (A) * alpha;

        float b0 =    A*((A+1.0f) - (A-1.0f)*cw + beta);
        float b1 =  2.0f*A*((A-1.0f) - (A+1.0f)*cw);
        float b2 =    A*((A+1.0f) - (A-1.0f)*cw - beta);
        float a0 =        (A+1.0f) + (A-1.0f)*cw + beta;
        float a1 =   -2.0f*((A-1.0f) + (A+1.0f)*cw);
        float a2 =        (A+1.0f) + (A-1.0f)*cw - beta;

        const float invA0 = 1.0f / a0;
        bq.b0 = b0 * invA0;
        bq.b1 = b1 * invA0;
        bq.b2 = b2 * invA0;
        bq.a1 = a1 * invA0;
        bq.a2 = a2 * invA0;
    }

    inline void makeHighShelf (Biquad& bq, float hz, float gainDb) noexcept
    {
        // RBJ shelf with slope S=1 (fixed topology; no extra params)
        const float A  = dbToA (gainDb);
        const float w0 = 2.0f * juce::MathConstants<float>::pi * hz / (float) sr;
        const float cw = std::cos (w0);
        const float sw = std::sin (w0);

        const float S = 1.0f;
        const float alpha = (sw / 2.0f) * std::sqrt ((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);
        const float beta  = 2.0f * std::sqrt (A) * alpha;

        float b0 =    A*((A+1.0f) + (A-1.0f)*cw + beta);
        float b1 = -2.0f*A*((A-1.0f) + (A+1.0f)*cw);
        float b2 =    A*((A+1.0f) + (A-1.0f)*cw - beta);
        float a0 =        (A+1.0f) - (A-1.0f)*cw + beta;
        float a1 =    2.0f*((A-1.0f) - (A+1.0f)*cw);
        float a2 =        (A+1.0f) - (A-1.0f)*cw - beta;

        const float invA0 = 1.0f / a0;
        bq.b0 = b0 * invA0;
        bq.b1 = b1 * invA0;
        bq.b2 = b2 * invA0;
        bq.a1 = a1 * invA0;
        bq.a2 = a2 * invA0;
    }

    // ---------- HPF 1st-order (pure math; already RT-safe) ----------
    inline void updateFirstOrderHPF (float hpfHz) noexcept
    {
        const double fc = (double) sanitizeHz (hpfHz);
        const double K  = std::tan (juce::MathConstants<double>::pi * fc / sr);

        const double a0 = 1.0 / (1.0 + K);
        hp1_b0 = (float) a0;
        hp1_b1 = (float) (-a0);
        hp1_a1 = (float) ((1.0 - K) / (1.0 + K));
    }

    inline float processFirstOrderHPF (int ch, float x) noexcept
    {
        const size_t i = (size_t) ch;

        // y[n] = b0*x[n] + b1*x[n-1] + a1*y[n-1]
        const float y = (hp1_b0 * x)
                      + (hp1_b1 * hp1_x1[i])
                      + (hp1_a1 * hp1_y1[i]);

        hp1_x1[i] = x;
        hp1_y1[i] = y;
        return y;
    }

    inline void invalidateAllLastValues() noexcept
    {
        lastHpfHz = -1.0f;
        lastLpfHz = -1.0f;

        lastLfFreq = -1.0f;
        lastLfGainDb = 9999.0f;

        lastLmfFreq = -1.0f;
        lastLmfGainDb = 9999.0f;
        lastLmfQ = -1.0f;

        lastHmfFreq = -1.0f;
        lastHmfGainDb = 9999.0f;
        lastHmfQ = -1.0f;

        lastHfFreq = -1.0f;
        lastHfGainDb = 9999.0f;
    }

    inline void rebuildAllBiquads() noexcept
    {
        const float hpfHz = sanitizeHz (hpfHzSm.getCurrentValue());
        const float lpfHz = sanitizeHz (lpfHzSm.getCurrentValue());

        const float lfF = sanitizeHz (lfFreqSm.getCurrentValue());
        const float lfG_user = sanitizeDb (lfGainDbSm.getCurrentValue());
        const float lfG_eff  = phase3ProtectBoostDb (phase3RestoreCutDb (lfG_user));

        const float lmfF  = sanitizeHz (lmfFreqSm.getCurrentValue());
        const float lmfG_user = sanitizeDb (lmfGainDbSm.getCurrentValue());
        const float lmfG_eff  = phase3ProtectBoostDb (phase3RestoreCutDb (lmfG_user));
        const float lmfQv_user = sanitizeQ  (lmfQSm.getCurrentValue());
        const float lmfQv_eff  = phase3WidenQForBoost (lmfQv_user, lmfG_eff);

        const float hmfF  = sanitizeHz (hmfFreqSm.getCurrentValue());
        const float hmfG_user = sanitizeDb (hmfGainDbSm.getCurrentValue());
        const float hmfG_eff  = phase3ProtectBoostDb (phase3RestoreCutDb (hmfG_user));
        const float hmfQv_user = sanitizeQ  (hmfQSm.getCurrentValue());
        const float hmfQv_eff  = phase3WidenQForBoost (hmfQv_user, hmfG_eff);

        const float hfF = sanitizeHz (hfFreqSm.getCurrentValue());
        const float hfG_user = sanitizeDb (hfGainDbSm.getCurrentValue());
        const float hfG_eff  = phase3ProtectBoostDb (phase3RestoreCutDb (hfG_user));
        for (int ch = 0; ch < channels; ++ch)
        {
            makeHighPass (hpf2[(size_t) ch], hpfHz, 0.70710678f);
            makeLowShelf (lfShelf[(size_t) ch], lfF, lfG_eff);
            makePeakingEQ(lmfPeak[(size_t) ch], lmfF, lmfQv_eff, lmfG_eff);
            makePeakingEQ(hmfPeak[(size_t) ch], hmfF, hmfQv_eff, hmfG_eff);
            makeHighShelf(hfShelf[(size_t) ch], hfF, hfG_eff);
            makeLowPass  (lpf2[(size_t) ch], lpfHz, 0.70710678f);
        }
    }

    inline void updateFiltersIfNeeded (int sampleIndex) noexcept
    {
        if ((sampleIndex % coeffUpdateIntervalSamples) != 0)
            return;

        const float hpfNow = hpfHzSm.getCurrentValue();
        const float lpfNow = lpfHzSm.getCurrentValue();

        // HPF/LPF true-off at endpoints (control-rate flags; no new params/UI)
        // Derive activity from sanitized Hz to match actual DSP behavior (sanitizeHz clamps).
        const float hpfHz = sanitizeHz (hpfNow);
        const float lpfHz = sanitizeHz (lpfNow);

        // Off endpoints come from current mapping/init values:
        //   HPF off at 20 Hz, LPF off at 20000 Hz
        // eps = tolerance for smoothed endpoint / float noise
        constexpr float kHpfOffHz = 20.0f;
        constexpr float kLpfOffHz = 20000.0f;
        constexpr float kHpfEpsHz = 0.01f;
        constexpr float kLpfEpsHz = 1.0f;

        const bool newHpfActive = (hpfHz > (kHpfOffHz + kHpfEpsHz));
        const bool newLpfActive = (lpfHz < (kLpfOffHz - kLpfEpsHz));

        // Reset states only on transition (active -> inactive), not continuously.
        if (lastHpfActive && ! newHpfActive)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                hpf2[(size_t) ch].reset();
                hp1_x1[(size_t) ch] = 0.0f;
                hp1_y1[(size_t) ch] = 0.0f;
            }
        }

        if (lastLpfActive && ! newLpfActive)
        {
            for (int ch = 0; ch < channels; ++ch)
                lpf2[(size_t) ch].reset();
        }

        hpfActive = newHpfActive;
        lpfActive = newLpfActive;
        lastHpfActive = newHpfActive;
        lastLpfActive = newLpfActive;

        const float lfF = lfFreqSm.getCurrentValue();
        const float lfG = lfGainDbSm.getCurrentValue();

        const float lmfF  = lmfFreqSm.getCurrentValue();
        const float lmfG  = lmfGainDbSm.getCurrentValue();
        const float lmfQv = lmfQSm.getCurrentValue();

        const float hmfF  = hmfFreqSm.getCurrentValue();
        const float hmfG  = hmfGainDbSm.getCurrentValue();
        const float hmfQv = hmfQSm.getCurrentValue();

        const float hfF = hfFreqSm.getCurrentValue();
        const float hfG = hfGainDbSm.getCurrentValue();

        const bool hpfChanged = !juce::approximatelyEqual (hpfNow, lastHpfHz);
        const bool lpfChanged = !juce::approximatelyEqual (lpfNow, lastLpfHz);

        const bool lfChanged  = !juce::approximatelyEqual (lfF, lastLfFreq) || !juce::approximatelyEqual (lfG, lastLfGainDb);
        const bool lmfChanged = !juce::approximatelyEqual (lmfF, lastLmfFreq) || !juce::approximatelyEqual (lmfG, lastLmfGainDb) || !juce::approximatelyEqual (lmfQv, lastLmfQ);
        const bool hmfChanged = !juce::approximatelyEqual (hmfF, lastHmfFreq) || !juce::approximatelyEqual (hmfG, lastHmfGainDb) || !juce::approximatelyEqual (hmfQv, lastHmfQ);
        const bool hfChanged  = !juce::approximatelyEqual (hfF, lastHfFreq) || !juce::approximatelyEqual (hfG, lastHfGainDb);

        // ----- Phase 3R control-rate cache + need detection (Patch 2) -----
        // Compute effective LMF/HMF values at control-rate (same transforms as rebuildAllBiquads)
        {
            const float lmfF_eff  = sanitizeHz (lmfF);
            const float lmfG_user = sanitizeDb (lmfG);
            const float lmfG_eff  = phase3ProtectBoostDb (phase3RestoreCutDb (lmfG_user));
            const float lmfQ_user = sanitizeQ (lmfQv);
            const float lmfQ_eff  = phase3WidenQForBoost (lmfQ_user, lmfG_eff);

            const float hmfF_eff  = sanitizeHz (hmfF);
            const float hmfG_user = sanitizeDb (hmfG);
            const float hmfG_eff  = phase3ProtectBoostDb (phase3RestoreCutDb (hmfG_user));
            const float hmfQ_user = sanitizeQ (hmfQv);
            const float hmfQ_eff  = phase3WidenQForBoost (hmfQ_user, hmfG_eff);

            phase3rLmfFreqHz     = lmfF_eff;
            phase3rLmfQEff       = lmfQ_eff;
            phase3rLmfGainEffDb  = lmfG_eff;
            phase3rLmfGateOpen   = (lmfG_eff > 1.0f);

            phase3rHmfFreqHz     = hmfF_eff;
            phase3rHmfQEff       = hmfQ_eff;
            phase3rHmfGainEffDb  = hmfG_eff;
            phase3rHmfGateOpen   = (hmfG_eff > 1.0f);
        }

        // Phase 3R control-rate constants (derived from sr) — used by sample-loop processing
        // Phase 3R control-rate constants (cached members for sample-loop use)
        phase3rEnvA_fast = std::exp (-1.0f / ((float) sr * 0.010f)); // ~10ms
        phase3rPersistDecay = std::exp (-1.0f / ((float) sr * 0.020f)); // ~20ms
        phase3rAtkA = std::exp (-1.0f / ((float) sr * 0.020f)); // ~20ms
        phase3rRelA = std::exp (-1.0f / ((float) sr * 0.800f)); // ~800ms
        phase3rDepthSlewPerSample = (10.0f / (float) sr); // 10 dB/s

        // Determine if Phase 3R needs control-rate coefficient refresh (depth changes), even if no EQ params changed.
        bool phase3rNeeds = false;
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto& sl = phase3rLmf[(size_t) ch];
            const auto& sh = phase3rHmf[(size_t) ch];
            if (std::fabs (sl.depthDb - sl.lastDepthDb) >= 0.02f || std::fabs (sh.depthDb - sh.lastDepthDb) >= 0.02f)
            {
                phase3rNeeds = true;
                break;
            }
        }

        // Early return must consider Phase 3R needs, otherwise suppression never engages unless user moves EQ knobs.
        if (! (hpfChanged || lpfChanged || lfChanged || lmfChanged || hmfChanged || hfChanged) && ! phase3rNeeds)
            return;

        // update last values
        if (hpfChanged) { lastHpfHz = hpfNow; updateFirstOrderHPF (hpfNow); }
        if (lpfChanged) { lastLpfHz = lpfNow; }

        if (lfChanged)  { lastLfFreq = lfF;   lastLfGainDb = lfG; }
        if (lmfChanged) { lastLmfFreq = lmfF; lastLmfGainDb = lmfG; lastLmfQ = lmfQv; }
        if (hmfChanged) { lastHmfFreq = hmfF; lastHmfGainDb = hmfG; lastHmfQ = hmfQv; }
        if (hfChanged)  { lastHfFreq = hfF;   lastHfGainDb = hfG; }

        // rebuild (pure math only) — only when EQ/filter params changed
        if (hpfChanged || lpfChanged || lfChanged || lmfChanged || hmfChanged || hfChanged)
            rebuildAllBiquads();

        // Phase 3R control-rate coefficient updates (detectors + suppressor), no per-sample rebuilds.
        // If only phase3rNeeds is true, we update only suppressor coefficients (depth-driven).
        for (int ch = 0; ch < channels; ++ch)
        {
            // LMF
            phase3rUpdateCoeffsForBand (phase3rLmf[(size_t) ch],
                                       phase3rLmfFreqHz,
                                       phase3rLmfQEff,
                                       phase3rLmfGainEffDb,
                                       phase3rLmfGateOpen,
                                       /*forceSuppressorOnly*/ !(lmfChanged));

            // HMF
            phase3rUpdateCoeffsForBand (phase3rHmf[(size_t) ch],
                                       phase3rHmfFreqHz,
                                       phase3rHmfQEff,
                                       phase3rHmfGainEffDb,
                                       phase3rHmfGateOpen,
                                       /*forceSuppressorOnly*/ !(hmfChanged));
        }
    }
    bool pureMode = false;



    double sr = 44100.0;
    int channels = 2;

    // trims + filters smoothers
    juce::SmoothedValue<float> inTrimLin;
    juce::SmoothedValue<float> outTrimLin;
    juce::SmoothedValue<float> hpfHzSm;
    juce::SmoothedValue<float> lpfHzSm;

    // HPF/LPF true-off flags (computed at control-rate; used in sample loop to skip processing)
    bool hpfActive = true;
    bool lpfActive = true;
    bool lastHpfActive = true;
    bool lastLpfActive = true;

    // band smoothers
    juce::SmoothedValue<float> lfFreqSm;
    juce::SmoothedValue<float> lfGainDbSm;

    juce::SmoothedValue<float> lmfFreqSm;
    juce::SmoothedValue<float> lmfGainDbSm;
    juce::SmoothedValue<float> lmfQSm;

    juce::SmoothedValue<float> hmfFreqSm;
    juce::SmoothedValue<float> hmfGainDbSm;
    juce::SmoothedValue<float> hmfQSm;

    juce::SmoothedValue<float> hfFreqSm;
    juce::SmoothedValue<float> hfGainDbSm;

    // per-channel biquads
    std::vector<Biquad> hpf2;
    std::vector<Biquad> lfShelf;
    std::vector<Biquad> lmfPeak;
    std::vector<Biquad> hmfPeak;

    // Phase 3R scaffold state (LMF/HMF only; wired later)
    std::vector<Phase3RBandState> phase3rLmf;
    std::vector<Phase3RBandState> phase3rHmf;

    // Phase 3R cached control-rate values (avoid recomputing effective gain inside sample loop)
    float phase3rLmfFreqHz = 1000.0f;
    float phase3rLmfQEff   = 1.0f;
    float phase3rLmfGainEffDb = 0.0f;
    bool  phase3rLmfGateOpen  = false;

    float phase3rHmfFreqHz = 4000.0f;
    float phase3rHmfQEff   = 1.0f;
    float phase3rHmfGainEffDb = 0.0f;
    bool  phase3rHmfGateOpen  = false;

    // Phase 3R cached control-rate constants (must be in scope for sample loop; no per-sample coeff rebuilds)
    float phase3rEnvA_fast = 0.0f;
    float phase3rPersistDecay = 0.0f;
    float phase3rAtkA = 0.0f;
    float phase3rRelA = 0.0f;
    float phase3rDepthSlewPerSample = 0.0f;


    std::vector<Biquad> hfShelf;
    std::vector<Biquad> lpf2;

    // 1st-order HPF coeffs/state
    float hp1_b0 = 1.0f;
    float hp1_b1 = -1.0f;
    float hp1_a1 = 0.0f;

    std::vector<float> hp1_x1;
    std::vector<float> hp1_y1;

    // coeff update throttling
    int coeffUpdateIntervalSamples = 16;

    // last-values for coefficient rebuild gating
    float lastHpfHz = -1.0f;
    float lastLpfHz = -1.0f;

    float lastLfFreq = -1.0f;
    float lastLfGainDb = 9999.0f;

    float lastLmfFreq = -1.0f;
    float lastLmfGainDb = 9999.0f;
    float lastLmfQ = -1.0f;

    float lastHmfFreq = -1.0f;
    float lastHmfGainDb = 9999.0f;
    float lastHmfQ = -1.0f;

    float lastHfFreq = -1.0f;
    float lastHfGainDb = 9999.0f;
};
} // namespace compass
