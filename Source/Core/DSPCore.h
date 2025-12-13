#pragma once
#include <JuceHeader.h>

namespace compass
{
    class DSPCore
    {
    public:
        void setParameterPointers(std::atomic<float> *inTrim,
                                  std::atomic<float> *outTrim,
                                  std::atomic<float> *hpfFreq,
                                  std::atomic<float> *lpfFreq) noexcept
        {
            pInTrim = inTrim;
            pOutTrim = outTrim;
            pHpfFreq = hpfFreq;
            pLpfFreq = lpfFreq;
        }

        void prepare(const juce::dsp::ProcessSpec &spec)
        {
            sampleRate = spec.sampleRate;

            hpfChain.prepare(spec);
            lpfFilter.prepare(spec);

            hpfChain.reset();
            lpfFilter.reset();

            hpfBiquad.state = std::make_shared<juce::dsp::IIR::Coefficients<float>>();
            hpfFirst.state = std::make_shared<juce::dsp::IIR::Coefficients<float>>();
            lpfFilter.state = std::make_shared<juce::dsp::IIR::Coefficients<float>>();

            // init smoothers (deterministic, fixed)
            inTrimSmooth.reset(sampleRate, kTrimSmoothSeconds);
            outTrimSmooth.reset(sampleRate, kTrimSmoothSeconds);
            hpfSmooth.reset(sampleRate, kFreqSmoothSeconds);
            lpfSmooth.reset(sampleRate, kFreqSmoothSeconds);

            const float initInDb = (pInTrim ? pInTrim->load() : 0.0f);
            const float initOutDb = (pOutTrim ? pOutTrim->load() : 0.0f);
            const float initHpf = (pHpfFreq ? pHpfFreq->load() : kHpfMinHz);
            const float initLpf = (pLpfFreq ? pLpfFreq->load() : kLpfMaxHz);

            inTrimSmooth.setCurrentAndTargetValue(initInDb);
            outTrimSmooth.setCurrentAndTargetValue(initOutDb);
            hpfSmooth.setCurrentAndTargetValue(initHpf);
            lpfSmooth.setCurrentAndTargetValue(initLpf);

            lastHpfTarget = initHpf;
            lastLpfTarget = initLpf;

            updateFilterCoefficients(clampHpf(initHpf), clampLpf(initLpf));
        }

        void process(juce::AudioBuffer<float> &buffer) noexcept
        {
            const int n = buffer.getNumSamples();
            if (n <= 0)
                return;

            // 1) Input trim (block ramp)
            const float inDbTarget = (pInTrim ? pInTrim->load() : 0.0f);
            const float inDbStart = inTrimSmooth.getCurrentValue();
            inTrimSmooth.setTargetValue(inDbTarget);
            inTrimSmooth.skip(n);
            const float inDbEnd = inTrimSmooth.getCurrentValue();

            buffer.applyGainRamp(0, n,
                                 juce::Decibels::decibelsToGain(inDbStart),
                                 juce::Decibels::decibelsToGain(inDbEnd));

            // 2/3) HPF/LPF (smooth targets, coeff update <= once per block)
            lastHpfTarget = (pHpfFreq ? pHpfFreq->load() : lastHpfTarget);
            lastLpfTarget = (pLpfFreq ? pLpfFreq->load() : lastLpfTarget);

            hpfSmooth.setTargetValue(lastHpfTarget);
            lpfSmooth.setTargetValue(lastLpfTarget);

            hpfSmooth.skip(n);
            lpfSmooth.skip(n);

            const float hpfHz = clampHpf(hpfSmooth.getCurrentValue());
            const float lpfHz = clampLpf(lpfSmooth.getCurrentValue());

            updateFilterCoefficients(hpfHz, lpfHz);

            juce::dsp::AudioBlock<float> block(buffer);
            juce::dsp::ProcessContextReplacing<float> ctx(block);

            hpfChain.process(ctx);
            lpfFilter.process(ctx);

            // 4) Output trim (block ramp)
            const float outDbTarget = (pOutTrim ? pOutTrim->load() : 0.0f);
            const float outDbStart = outTrimSmooth.getCurrentValue();
            outTrimSmooth.setTargetValue(outDbTarget);
            outTrimSmooth.skip(n);
            const float outDbEnd = outTrimSmooth.getCurrentValue();

            buffer.applyGainRamp(0, n,
                                 juce::Decibels::decibelsToGain(outDbStart),
                                 juce::Decibels::decibelsToGain(outDbEnd));
        }

    private:
        static constexpr double kTrimSmoothSeconds = 0.020;
        static constexpr double kFreqSmoothSeconds = 0.050;

        // locked ranges from the Build Plan
        static constexpr float kHpfMinHz = 20.0f;
        static constexpr float kHpfMaxHz = 1000.0f;
        static constexpr float kLpfMinHz = 3000.0f;
        static constexpr float kLpfMaxHz = 20000.0f;

        std::atomic<float> *pInTrim = nullptr;
        std::atomic<float> *pOutTrim = nullptr;
        std::atomic<float> *pHpfFreq = nullptr;
        std::atomic<float> *pLpfFreq = nullptr;

        double sampleRate = 44100.0;
        float lastHpfTarget = kHpfMinHz;
        float lastLpfTarget = kLpfMaxHz;

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inTrimSmooth, outTrimSmooth;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> hpfSmooth, lpfSmooth;

        // HPF 18 dB/oct = 2nd + 1st order
        juce::dsp::ProcessorChain<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Filter<float>> hpfChain;
        juce::dsp::IIR::Filter<float> &hpfBiquad = hpfChain.get<0>();
        juce::dsp::IIR::Filter<float> &hpfFirst = hpfChain.get<1>();

        // LPF 12 dB/oct
        juce::dsp::IIR::Filter<float> lpfFilter;

        float clampHpf(float hz) const noexcept
        {
            const float nyqCap = static_cast<float>(0.49 * 0.5 * sampleRate);
            return juce::jlimit(kHpfMinHz, juce::jmin(kHpfMaxHz, nyqCap), hz);
        }

        float clampLpf(float hz) const noexcept
        {
            const float nyqCap = static_cast<float>(0.49 * 0.5 * sampleRate);
            return juce::jlimit(kLpfMinHz, juce::jmin(kLpfMaxHz, nyqCap), hz);
        }

        void updateFilterCoefficients(float hpfHz, float lpfHz) noexcept
        {
            const auto hp2 = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass(sampleRate, hpfHz);
            const auto hp1 = juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderHighPass(sampleRate, hpfHz);
            const auto lp2 = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass(sampleRate, lpfHz);

            *hpfBiquad.state = juce::dsp::IIR::Coefficients<float>(hp2);
            *hpfFirst.state = juce::dsp::IIR::Coefficients<float>(hp1);
            *lpfFilter.state = juce::dsp::IIR::Coefficients<float>(lp2);
        }
    };
} // namespace compass