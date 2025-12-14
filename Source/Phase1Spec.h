#pragma once
#include <JuceHeader.h>

namespace phase1
{
    // Canonical IDs (LOCKED by Phase 1 Numeric Parameter Spec Addendum — V1.0)
    static constexpr const char *LF_FREQUENCY_ID = "lf_freq";
    static constexpr const char *LF_GAIN_ID = "lf_gain";

    static constexpr const char *LMF_FREQUENCY_ID = "lmf_freq";
    static constexpr const char *LMF_GAIN_ID = "lmf_gain";
    static constexpr const char *LMF_Q_ID = "lmf_q";

    static constexpr const char *HMF_FREQUENCY_ID = "hmf_freq";
    static constexpr const char *HMF_GAIN_ID = "hmf_gain";
    static constexpr const char *HMF_Q_ID = "hmf_q";

    static constexpr const char *HF_FREQUENCY_ID = "hf_freq";
    static constexpr const char *HF_GAIN_ID = "hf_gain";

    static constexpr const char *HPF_FREQUENCY_ID = "hpf_freq";
    static constexpr const char *LPF_FREQUENCY_ID = "lpf_freq";

    static constexpr const char *INPUT_TRIM_ID = "input_trim";
    static constexpr const char *OUTPUT_TRIM_ID = "output_trim";

    static constexpr const char *GLOBAL_BYPASS_ID = "global_bypass";

    // Phase 1 provisional numeric ranges/defaults (authorized for Phase 1 only)
    struct Ranges
    {
        static constexpr float LF_FREQ_MIN = 20.0f;
        static constexpr float LF_FREQ_MAX = 800.0f;
        static constexpr float LF_FREQ_DEF = 100.0f;

        static constexpr float LMF_FREQ_MIN = 120.0f;
        static constexpr float LMF_FREQ_MAX = 4000.0f;
        static constexpr float LMF_FREQ_DEF = 1000.0f;

        static constexpr float HMF_FREQ_MIN = 600.0f;
        static constexpr float HMF_FREQ_MAX = 15000.0f;
        static constexpr float HMF_FREQ_DEF = 3000.0f;

        static constexpr float HF_FREQ_MIN = 1500.0f;
        static constexpr float HF_FREQ_MAX = 22000.0f;
        static constexpr float HF_FREQ_DEF = 8000.0f;

        static constexpr float GAIN_MIN = -18.0f;
        static constexpr float GAIN_MAX = 18.0f;
        static constexpr float GAIN_DEF = 0.0f;

        static constexpr float Q_MIN = 0.5f;
        static constexpr float Q_MAX = 2.0f;
        static constexpr float Q_DEF = 1.0f;

        static constexpr float HPF_MIN = 20.0f;
        static constexpr float HPF_MAX = 1000.0f;
        static constexpr float HPF_DEF = 20.0f;

        static constexpr float LPF_MIN = 3000.0f;
        static constexpr float LPF_MAX = 20000.0f;
        static constexpr float LPF_DEF = 20000.0f;

        static constexpr float TRIM_MIN = -20.0f;
        static constexpr float TRIM_MAX = 20.0f;
        static constexpr float TRIM_DEF = 0.0f;
    };

    inline juce::NormalisableRange<float> makeHzRange(float minHz, float maxHz)
    {
        // Phase 1: simple linear range; no sonic claims. Deterministic.
        return juce::NormalisableRange<float>(minHz, maxHz, 0.0f);
    }

    inline juce::NormalisableRange<float> makeDbRange(float minDb, float maxDb)
    {
        return juce::NormalisableRange<float>(minDb, maxDb, 0.01f);
    }

    inline juce::NormalisableRange<float> makeQRange(float minQ, float maxQ)
    {
        return juce::NormalisableRange<float>(minQ, maxQ, 0.001f);
    }
}