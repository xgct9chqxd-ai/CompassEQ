#pragma once
#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <cmath>

namespace compass
{
class DSPCore final
{
public:
    DSPCore() = default;

    inline void prepare (double sampleRate, int /*samplesPerBlock*/, int numChannels)
    {
        sr = (sampleRate > 0.0 ? sampleRate : 44100.0);
        channels = juce::jmax (1, numChannels);

        // per-channel 1st-order HPF state
        hp1_x1.assign ((size_t) channels, 0.0f);
        hp1_y1.assign ((size_t) channels, 0.0f);

        // per-channel IIR filters (must NOT share state across channels)
        hpf2.clear();
        lpf2.clear();
        lfShelf.clear();
        lmfPeak.clear();
        hmfPeak.clear();
        hfShelf.clear();

        hpf2.reserve    ((size_t) channels);
        lpf2.reserve    ((size_t) channels);
        lfShelf.reserve ((size_t) channels);
        lmfPeak.reserve ((size_t) channels);
        hmfPeak.reserve ((size_t) channels);
        hfShelf.reserve ((size_t) channels);

        for (int ch = 0; ch < channels; ++ch)
        {
            hpf2.emplace_back    (std::make_unique<juce::IIRFilter>());
            lfShelf.emplace_back (std::make_unique<juce::IIRFilter>());
            lmfPeak.emplace_back (std::make_unique<juce::IIRFilter>());
            hmfPeak.emplace_back (std::make_unique<juce::IIRFilter>());
            hfShelf.emplace_back (std::make_unique<juce::IIRFilter>());
            lpf2.emplace_back    (std::make_unique<juce::IIRFilter>());
        }

        constexpr double smoothTimeSec = 0.02; // 20 ms (no zipper; minimal lag)

        // trims + filters
        inTrimLin.reset  (sr, smoothTimeSec);
        outTrimLin.reset (sr, smoothTimeSec);
        hpfHzSm.reset    (sr, smoothTimeSec);
        lpfHzSm.reset    (sr, smoothTimeSec);

        // bands
        lfFreqSm.reset   (sr, smoothTimeSec);
        lfGainDbSm.reset (sr, smoothTimeSec);

        lmfFreqSm.reset  (sr, smoothTimeSec);
        lmfGainDbSm.reset(sr, smoothTimeSec);
        lmfQSm.reset     (sr, smoothTimeSec);

        hmfFreqSm.reset  (sr, smoothTimeSec);
        hmfGainDbSm.reset(sr, smoothTimeSec);
        hmfQSm.reset     (sr, smoothTimeSec);

        hfFreqSm.reset   (sr, smoothTimeSec);
        hfGainDbSm.reset (sr, smoothTimeSec);

        // defaults (Phase 1 numeric defaults are owned upstream; we only set safe initial values here)
        inTrimLin.setCurrentAndTargetValue  (1.0f);
        outTrimLin.setCurrentAndTargetValue (1.0f);

        hpfHzSm.setCurrentAndTargetValue (HPF_MIN);
        lpfHzSm.setCurrentAndTargetValue (LPF_MAX);

        lfFreqSm.setCurrentAndTargetValue  (LF_FREQ_DEF);
        lfGainDbSm.setCurrentAndTargetValue(0.0f);

        lmfFreqSm.setCurrentAndTargetValue (LMF_FREQ_DEF);
        lmfGainDbSm.setCurrentAndTargetValue(0.0f);
        lmfQSm.setCurrentAndTargetValue    (Q_DEF);

        hmfFreqSm.setCurrentAndTargetValue (HMF_FREQ_DEF);
        hmfGainDbSm.setCurrentAndTargetValue(0.0f);
        hmfQSm.setCurrentAndTargetValue    (Q_DEF);

        hfFreqSm.setCurrentAndTargetValue  (HF_FREQ_DEF);
        hfGainDbSm.setCurrentAndTargetValue(0.0f);

        // force initial coefficient build
        invalidateAllLastValues();

        updateFirstOrderHPF (HPF_MIN);

        // Init coeffs once
        const auto hp = juce::IIRCoefficients::makeHighPass (sr, (double) HPF_MIN, (double) BUTTER_Q);
        const auto lp = juce::IIRCoefficients::makeLowPass  (sr, (double) LPF_MAX, (double) BUTTER_Q);

        const auto lf = juce::IIRCoefficients::makeLowShelf  (sr, (double) LF_FREQ_DEF, (double) SHELF_Q, 1.0);
        const auto hf = juce::IIRCoefficients::makeHighShelf (sr, (double) HF_FREQ_DEF, (double) SHELF_Q, 1.0);

        const auto lmf = juce::IIRCoefficients::makePeakFilter (sr, (double) LMF_FREQ_DEF, (double) Q_DEF, 1.0);
        const auto hmf = juce::IIRCoefficients::makePeakFilter (sr, (double) HMF_FREQ_DEF, (double) Q_DEF, 1.0);

        for (int ch = 0; ch < channels; ++ch)
        {
            hpf2[(size_t) ch]->setCoefficients (hp);
            lfShelf[(size_t) ch]->setCoefficients (lf);
            lmfPeak[(size_t) ch]->setCoefficients (lmf);
            hmfPeak[(size_t) ch]->setCoefficients (hmf);
            hfShelf[(size_t) ch]->setCoefficients (hf);
            lpf2[(size_t) ch]->setCoefficients (lp);
        }

        reset();
    }

    inline void reset() noexcept
    {
        for (auto& f : hpf2)    if (f) f->reset();
        for (auto& f : lfShelf) if (f) f->reset();
        for (auto& f : lmfPeak) if (f) f->reset();
        for (auto& f : hmfPeak) if (f) f->reset();
        for (auto& f : hfShelf) if (f) f->reset();
        for (auto& f : lpf2)    if (f) f->reset();

        for (int ch = 0; ch < (int) hp1_x1.size(); ++ch)
        {
            hp1_x1[(size_t) ch] = 0.0f;
            hp1_y1[(size_t) ch] = 0.0f;
        }
    }

    // Phase 2A-2 targets (trims + HPF/LPF)
    inline void setTargets (float inTrimDb, float outTrimDb, float hpfHz, float lpfHz) noexcept
    {
        inTrimLin.setTargetValue  (juce::Decibels::decibelsToGain (inTrimDb));
        outTrimLin.setTargetValue (juce::Decibels::decibelsToGain (outTrimDb));
        hpfHzSm.setTargetValue (juce::jlimit (HPF_MIN, HPF_MAX, hpfHz));
        lpfHzSm.setTargetValue (juce::jlimit (LPF_MIN, LPF_MAX, lpfHz));
    }

    // Phase 2B targets (EQ bands)
    inline void setBandTargets (
        float lfFreqHz, float lfGainDb,
        float lmfFreqHz, float lmfGainDb, float lmfQ,
        float hmfFreqHz, float hmfGainDb, float hmfQ,
        float hfFreqHz, float hfGainDb) noexcept
    {
        lfFreqSm.setTargetValue    (juce::jlimit (LF_FREQ_MIN,  LF_FREQ_MAX,  lfFreqHz));
        lfGainDbSm.setTargetValue  (juce::jlimit (GAIN_MIN,     GAIN_MAX,     lfGainDb));

        lmfFreqSm.setTargetValue   (juce::jlimit (LMF_FREQ_MIN, LMF_FREQ_MAX, lmfFreqHz));
        lmfGainDbSm.setTargetValue (juce::jlimit (GAIN_MIN,     GAIN_MAX,     lmfGainDb));
        lmfQSm.setTargetValue      (juce::jlimit (Q_MIN,        Q_MAX,        lmfQ));

        hmfFreqSm.setTargetValue   (juce::jlimit (HMF_FREQ_MIN, HMF_FREQ_MAX, hmfFreqHz));
        hmfGainDbSm.setTargetValue (juce::jlimit (GAIN_MIN,     GAIN_MAX,     hmfGainDb));
        hmfQSm.setTargetValue      (juce::jlimit (Q_MIN,        Q_MAX,        hmfQ));

        hfFreqSm.setTargetValue    (juce::jlimit (HF_FREQ_MIN,  HF_FREQ_MAX,  hfFreqHz));
        hfGainDbSm.setTargetValue  (juce::jlimit (GAIN_MIN,     GAIN_MAX,     hfGainDb));
    }

    inline void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n   = buffer.getNumSamples();
        const int chs = juce::jmin (channels, buffer.getNumChannels());

        for (int i = 0; i < n; ++i)
        {
            const float inG  = inTrimLin.getNextValue();
            const float outG = outTrimLin.getNextValue();

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

            updateFiltersIfNeeded (i);

            for (int ch = 0; ch < chs; ++ch)
            {
                float x = buffer.getSample (ch, i);

                // Input Trim
                x *= inG;

                // HPF 18 dB/oct = 12 dB biquad + 6 dB first-order
                x = hpf2[(size_t) ch]->processSingleSampleRaw (x);
                x = processFirstOrderHPF (ch, x);

                // EQ Bands (fixed topology)
                x = lfShelf[(size_t) ch]->processSingleSampleRaw (x);
                x = lmfPeak[(size_t) ch]->processSingleSampleRaw (x);
                x = hmfPeak[(size_t) ch]->processSingleSampleRaw (x);
                x = hfShelf[(size_t) ch]->processSingleSampleRaw (x);

                // LPF 12 dB/oct biquad
                x = lpf2[(size_t) ch]->processSingleSampleRaw (x);

                // Output Trim
                x *= outG;

                buffer.setSample (ch, i, x);
            }
        }
    }

private:
    // HPF/LPF
    static constexpr float HPF_MIN = 20.0f;
    static constexpr float HPF_MAX = 1000.0f;
    static constexpr float LPF_MIN = 3000.0f;
    static constexpr float LPF_MAX = 20000.0f;

    // bands (Phase 1 numeric spec)
    static constexpr float LF_FREQ_MIN  = 20.0f;
    static constexpr float LF_FREQ_MAX  = 800.0f;
    static constexpr float LF_FREQ_DEF  = 100.0f;

    static constexpr float LMF_FREQ_MIN = 120.0f;
    static constexpr float LMF_FREQ_MAX = 4000.0f;
    static constexpr float LMF_FREQ_DEF = 1000.0f;

    static constexpr float HMF_FREQ_MIN = 600.0f;
    static constexpr float HMF_FREQ_MAX = 15000.0f;
    static constexpr float HMF_FREQ_DEF = 3000.0f;

    static constexpr float HF_FREQ_MIN  = 1500.0f;
    static constexpr float HF_FREQ_MAX  = 22000.0f;
    static constexpr float HF_FREQ_DEF  = 8000.0f;

    static constexpr float GAIN_MIN = -18.0f;
    static constexpr float GAIN_MAX = 18.0f;

    static constexpr float Q_MIN  = 0.5f;
    static constexpr float Q_MAX  = 2.0f;
    static constexpr float Q_DEF  = 1.0f;

    static constexpr float BUTTER_Q = 0.7071067811865476f;
    static constexpr float SHELF_Q  = 0.7071067811865476f; // fixed musical shelf Q (non-selectable)

    inline static double dbToGain (float db) noexcept
    {
        return (double) juce::Decibels::decibelsToGain (db);
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

    inline void updateFirstOrderHPF (float hpfHz) noexcept
    {
        const double fc = (double) juce::jlimit (HPF_MIN, HPF_MAX, hpfHz);
        const double K  = std::tan (juce::MathConstants<double>::pi * fc / sr);

        const double a0 = 1.0 / (1.0 + K);
        hp1_b0 = (float) a0;
        hp1_b1 = (float) (-a0);
        hp1_a1 = (float) ((1.0 - K) / (1.0 + K));
    }

    inline void updateFiltersIfNeeded (int sampleIndex) noexcept
    {
        if ((sampleIndex % coeffUpdateIntervalSamples) != 0)
            return;

        const float hpfNow = hpfHzSm.getCurrentValue();
        const float lpfNow = lpfHzSm.getCurrentValue();

        const float lfF = lfFreqSm.getCurrentValue();
        const float lfG = lfGainDbSm.getCurrentValue();

        const float lmfF = lmfFreqSm.getCurrentValue();
        const float lmfG = lmfGainDbSm.getCurrentValue();
        const float lmfQv= lmfQSm.getCurrentValue();

        const float hmfF = hmfFreqSm.getCurrentValue();
        const float hmfG = hmfGainDbSm.getCurrentValue();
        const float hmfQv= hmfQSm.getCurrentValue();

        const float hfF = hfFreqSm.getCurrentValue();
        const float hfG = hfGainDbSm.getCurrentValue();

        const bool hpfChanged = !juce::approximatelyEqual (hpfNow, lastHpfHz);
        const bool lpfChanged = !juce::approximatelyEqual (lpfNow, lastLpfHz);

        const bool lfChanged  = !juce::approximatelyEqual (lfF, lastLfFreq) || !juce::approximatelyEqual (lfG, lastLfGainDb);
        const bool lmfChanged = !juce::approximatelyEqual (lmfF, lastLmfFreq) || !juce::approximatelyEqual (lmfG, lastLmfGainDb) || !juce::approximatelyEqual (lmfQv, lastLmfQ);
        const bool hmfChanged = !juce::approximatelyEqual (hmfF, lastHmfFreq) || !juce::approximatelyEqual (hmfG, lastHmfGainDb) || !juce::approximatelyEqual (hmfQv, lastHmfQ);
        const bool hfChanged  = !juce::approximatelyEqual (hfF, lastHfFreq) || !juce::approximatelyEqual (hfG, lastHfGainDb);

        if (! (hpfChanged || lpfChanged || lfChanged || lmfChanged || hmfChanged || hfChanged))
            return;

        if (hpfChanged)
        {
            lastHpfHz = hpfNow;
            updateFirstOrderHPF (hpfNow);
        }
        if (lpfChanged)
        {
            lastLpfHz = lpfNow;
        }

        if (lfChanged)
        {
            lastLfFreq = lfF;
            lastLfGainDb = lfG;
        }
        if (lmfChanged)
        {
            lastLmfFreq = lmfF;
            lastLmfGainDb = lmfG;
            lastLmfQ = lmfQv;
        }
        if (hmfChanged)
        {
            lastHmfFreq = hmfF;
            lastHmfGainDb = hmfG;
            lastHmfQ = hmfQv;
        }
        if (hfChanged)
        {
            lastHfFreq = hfF;
            lastHfGainDb = hfG;
        }

        const auto hp = juce::IIRCoefficients::makeHighPass (sr, (double) hpfNow, (double) BUTTER_Q);
        const auto lp = juce::IIRCoefficients::makeLowPass  (sr, (double) lpfNow, (double) BUTTER_Q);

        const auto lf = juce::IIRCoefficients::makeLowShelf  (sr, (double) lfF, (double) SHELF_Q, dbToGain (lfG));
        const auto hf = juce::IIRCoefficients::makeHighShelf (sr, (double) hfF, (double) SHELF_Q, dbToGain (hfG));

        const auto lmf = juce::IIRCoefficients::makePeakFilter (sr, (double) lmfF, (double) lmfQv, dbToGain (lmfG));
        const auto hmf = juce::IIRCoefficients::makePeakFilter (sr, (double) hmfF, (double) hmfQv, dbToGain (hmfG));

        for (int ch = 0; ch < channels; ++ch)
        {
            hpf2[(size_t) ch]->setCoefficients (hp);
            lfShelf[(size_t) ch]->setCoefficients (lf);
            lmfPeak[(size_t) ch]->setCoefficients (lmf);
            hmfPeak[(size_t) ch]->setCoefficients (hmf);
            hfShelf[(size_t) ch]->setCoefficients (hf);
            lpf2[(size_t) ch]->setCoefficients (lp);
        }
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

    double sr = 44100.0;
    int channels = 2;

    // trims + filters smoothers
    juce::SmoothedValue<float> inTrimLin;
    juce::SmoothedValue<float> outTrimLin;
    juce::SmoothedValue<float> hpfHzSm;
    juce::SmoothedValue<float> lpfHzSm;

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

    // per-channel filters
    std::vector<std::unique_ptr<juce::IIRFilter>> hpf2;
    std::vector<std::unique_ptr<juce::IIRFilter>> lfShelf;
    std::vector<std::unique_ptr<juce::IIRFilter>> lmfPeak;
    std::vector<std::unique_ptr<juce::IIRFilter>> hmfPeak;
    std::vector<std::unique_ptr<juce::IIRFilter>> hfShelf;
    std::vector<std::unique_ptr<juce::IIRFilter>> lpf2;

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
