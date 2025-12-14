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
        hpf2.reserve ((size_t) channels);
        lpf2.reserve ((size_t) channels);

        for (int ch = 0; ch < channels; ++ch)
        {
            hpf2.emplace_back (std::make_unique<juce::IIRFilter>());
            lpf2.emplace_back (std::make_unique<juce::IIRFilter>());
        }

        constexpr double smoothTimeSec = 0.02; // 20 ms (no zipper; minimal lag)
        inTrimLin.reset  (sr, smoothTimeSec);
        outTrimLin.reset (sr, smoothTimeSec);
        hpfHzSm.reset    (sr, smoothTimeSec);
        lpfHzSm.reset    (sr, smoothTimeSec);

        inTrimLin.setCurrentAndTargetValue  (1.0f);
        outTrimLin.setCurrentAndTargetValue (1.0f);
        hpfHzSm.setCurrentAndTargetValue (HPF_MIN);
        lpfHzSm.setCurrentAndTargetValue (LPF_MAX);

        lastHpfHz = -1.0f;
        lastLpfHz = -1.0f;

        updateFirstOrderHPF (HPF_MIN);

        const auto hp = juce::IIRCoefficients::makeHighPass (sr, (double) HPF_MIN, (double) BUTTER_Q);
        const auto lp = juce::IIRCoefficients::makeLowPass  (sr, (double) LPF_MAX, (double) BUTTER_Q);

        for (int ch = 0; ch < channels; ++ch)
        {
            hpf2[(size_t) ch]->setCoefficients (hp);
            lpf2[(size_t) ch]->setCoefficients (lp);
        }

        reset();
    }

    inline void reset() noexcept
    {
        for (auto& f : hpf2) if (f) f->reset();
        for (auto& f : lpf2) if (f) f->reset();

        for (int ch = 0; ch < (int) hp1_x1.size(); ++ch)
        {
            hp1_x1[(size_t) ch] = 0.0f;
            hp1_y1[(size_t) ch] = 0.0f;
        }
    }

    inline void setTargets (float inTrimDb, float outTrimDb,
                            float hpfHz, float lpfHz) noexcept
    {
        inTrimLin.setTargetValue  (juce::Decibels::decibelsToGain (inTrimDb));
        outTrimLin.setTargetValue (juce::Decibels::decibelsToGain (outTrimDb));
        hpfHzSm.setTargetValue (juce::jlimit (HPF_MIN, HPF_MAX, hpfHz));
        lpfHzSm.setTargetValue (juce::jlimit (LPF_MIN, LPF_MAX, lpfHz));
    }

    inline void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n   = buffer.getNumSamples();
        const int chs = juce::jmin (channels, buffer.getNumChannels());

        for (int i = 0; i < n; ++i)
        {
            const float inG  = inTrimLin.getNextValue();
            const float outG = outTrimLin.getNextValue();

            // advance smoothers
            hpfHzSm.getNextValue();
            lpfHzSm.getNextValue();

            updateFiltersIfNeeded (i);

            for (int ch = 0; ch < chs; ++ch)
            {
                float x = buffer.getSample (ch, i);

                x *= inG;

                // HPF 18 dB/oct = 12 dB biquad + 6 dB first-order
                x = hpf2[(size_t) ch]->processSingleSampleRaw (x);
                x = processFirstOrderHPF (ch, x);

                // LPF 12 dB/oct biquad
                x = lpf2[(size_t) ch]->processSingleSampleRaw (x);

                x *= outG;

                buffer.setSample (ch, i, x);
            }
        }
    }

private:
    static constexpr float HPF_MIN = 20.0f;
    static constexpr float HPF_MAX = 1000.0f;
    static constexpr float LPF_MIN = 3000.0f;
    static constexpr float LPF_MAX = 20000.0f;

    static constexpr float BUTTER_Q = 0.7071067811865476f;

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

        if (juce::approximatelyEqual (hpfNow, lastHpfHz) &&
            juce::approximatelyEqual (lpfNow, lastLpfHz))
            return;

        lastHpfHz = hpfNow;
        lastLpfHz = lpfNow;

        updateFirstOrderHPF (hpfNow);

        const auto hp = juce::IIRCoefficients::makeHighPass (sr, (double) hpfNow, (double) BUTTER_Q);
        const auto lp = juce::IIRCoefficients::makeLowPass  (sr, (double) lpfNow, (double) BUTTER_Q);

        for (int ch = 0; ch < channels; ++ch)
        {
            hpf2[(size_t) ch]->setCoefficients (hp);
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

    juce::SmoothedValue<float> inTrimLin;
    juce::SmoothedValue<float> outTrimLin;
    juce::SmoothedValue<float> hpfHzSm;
    juce::SmoothedValue<float> lpfHzSm;

    std::vector<std::unique_ptr<juce::IIRFilter>> hpf2;
    std::vector<std::unique_ptr<juce::IIRFilter>> lpf2;

    float hp1_b0 = 1.0f;
    float hp1_b1 = -1.0f;
    float hp1_a1 = 0.0f;

    std::vector<float> hp1_x1;
    std::vector<float> hp1_y1;

    int coeffUpdateIntervalSamples = 16;
    float lastHpfHz = -1.0f;
    float lastLpfHz = -1.0f;
};
} // namespace compass
