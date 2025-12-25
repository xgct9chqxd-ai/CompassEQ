#pragma once
#include <JuceHeader.h>

namespace UIStyle
{
    // ===== Alpha bounds (constitution compliance) =====
    constexpr float highlightAlphaMax = 0.12f;
    constexpr float occlusionAlphaMax = 0.18f;

    // ===== Colors =====
    namespace Colors
    {
        const auto background = juce::Colours::black;
        const auto foreground = juce::Colours::white;

        // Knob colors
        const auto knobBody = juce::Colour::fromRGB (38, 38, 38);
        const auto knobOcclusion = juce::Colour::fromRGB (18, 18, 18);
        const auto knobOuterRim = juce::Colours::black;
        const auto knobLipHighlight = juce::Colour::fromRGB (55, 55, 55);
        const auto knobInnerShadow = juce::Colour::fromRGB (28, 28, 28);
        const auto knobIndicator = juce::Colour::fromRGB (235, 235, 235);
        const auto knobIndicatorUnderStroke = juce::Colour::fromRGB (22, 22, 22);

        // ===== Stage 5 hue sources (LOCKED) =====
        // Knob rendering must remain neutral; backgrounds may use explicit band hue constants.
        // These are hue-angle locks (OKLCH hue degrees) and are the ONLY legal hue sources for band backgrounds.
        // Band → hue mapping (compass scheme)
        // LF = blue, LMF = purple, HMF = green, HF = red
        constexpr float bandHueLF  = 240.0f; // blue
        constexpr float bandHueLMF = 300.0f; // purple (more magenta-leaning to avoid reading blue after gamut/boost)
        constexpr float bandHueHMF = 120.0f; // green
        constexpr float bandHueHF  =   0.0f; // red
    }

    // ===== Text alphas (paint hygiene ladder) =====
    namespace TextAlpha
    {
        constexpr float title = 0.90f;
        constexpr float header = 0.82f; // Pass 3: section labels slightly higher contrast
        constexpr float micro = 0.52f;  // Pass 3: primary scale text readability (still below headers)
        constexpr float tick = 0.30f;
    }

    // ===== UI element alphas =====
    namespace UIAlpha
    {
        constexpr float globalBorder = 0.12f;
        constexpr float microSeparator = 0.06f;
        constexpr float debugOverlay = 0.20f;
        constexpr float auditOverlay = 0.20f;
        constexpr float auditOverlayKnob = 0.14f;
        constexpr float auditOverlayMeter = 0.18f;
    }

    // ===== Plate styles =====
    namespace Plate
    {
        constexpr float strokeWidth = 1.0f;
        
        namespace Radius
        {
            constexpr float background = 10.0f;
            constexpr float header = 10.0f;
            constexpr float zone = 8.0f;
            constexpr float sub = 6.0f;
            constexpr float well = 4.0f;
        }

        namespace FillAlpha
        {
            constexpr float background = 0.015f;
            constexpr float header = 0.030f;
            constexpr float zone = 0.022f;
            constexpr float sub = 0.018f;
            constexpr float well = 0.060f;
        }

        namespace StrokeAlpha
        {
            constexpr float background = 0.07f;
            constexpr float header = 0.10f;
            constexpr float zone = 0.10f;
            constexpr float sub = 0.10f;
            constexpr float well = 0.16f;
        }
    }

    // ===== Phase 2: Pixel Snapping + Discrete Ladders =====
    namespace Snap
    {
        // Snap to device pixel grid
        inline float snapPx (float x, float physicalScale)
        {
            return std::round (x * physicalScale) / physicalScale;
        }

        inline float snapPxCeil (float x, float physicalScale)
        {
            return std::ceil (x * physicalScale) / physicalScale;
        }

        inline float snapPxFloor (float x, float physicalScale)
        {
            return std::floor (x * physicalScale) / physicalScale;
        }

        inline int snapIntPx (int x, float physicalScale)
        {
            return juce::roundToInt (std::round ((float) x * physicalScale) / physicalScale);
        }

        inline juce::Point<float> snapPoint (juce::Point<float> p, float physicalScale)
        {
            return { snapPx (p.x, physicalScale), snapPx (p.y, physicalScale) };
        }
    }

    // ===== Phase 2: Discrete Stroke Ladder (by scaleKey, not radius) =====
    namespace StrokeLadder
    {
        // Discrete stroke widths keyed by scaleKey (1.00, 2.00, etc.)
        inline float hairlineStroke (float scaleKey)
        {
            if (scaleKey >= 1.75f) return 0.5f;  // 2.00: 0.5 logical = 1.0 physical
            return 1.0f;  // 1.00: 1.0 logical = 1.0 physical
        }

        inline float ringStrokeOuter (float scaleKey)
        {
            if (scaleKey >= 1.75f) return 2.0f;  // 2.00
            return 1.5f;  // 1.00
        }

        inline float ringStrokeLip (float scaleKey)
        {
            if (scaleKey >= 1.75f) return 1.5f;  // 2.00
            return 1.0f;  // 1.00
        }

        inline float ringStrokeInner (float scaleKey)
        {
            if (scaleKey >= 1.75f) return 1.5f;  // 2.00
            return 1.0f;  // 1.00
        }

        inline float indicatorStroke (float scaleKey)
        {
            if (scaleKey >= 1.75f) return 2.0f;  // 2.00
            return 1.6f;  // 1.00
        }

        inline float indicatorUnderStroke (float scaleKey)
        {
            return indicatorStroke (scaleKey) + 0.4f;
        }

        inline float plateBorderStroke (float scaleKey)
        {
            if (scaleKey >= 1.75f) return 1.0f;  // 2.00
            return 1.0f;  // 1.00
        }
    }

    // ===== Phase 2: Discrete Font Ladder (by scaleKey) =====
    // Phase 3: Static prebuilt tables to avoid per-paint construction
    namespace FontLadder
    {
        // Pass 3: One clean, neutral sans-serif family across the UI (no mixing).
        // Use a system-safe face; JUCE will fall back if not available.
        inline juce::Font makeUiSans (float height, int styleFlags)
        {
            juce::Font f ("Arial", height, styleFlags);
            // Slightly condensed feel without switching families (helps keep existing fitted text stable).
            f.setHorizontalScale (0.95f);
            return f;
        }

        // Prebuilt font tables for 1.00 and 2.00 scale keys (ODR-safe inline variables)
        inline const juce::Font titleFont_1_00  = makeUiSans (18.0f, juce::Font::bold);
        inline const juce::Font titleFont_2_00  = makeUiSans (18.0f, juce::Font::bold);
        inline const juce::Font headerFont_1_00 = makeUiSans (11.0f, juce::Font::bold);
        inline const juce::Font headerFont_2_00 = makeUiSans (11.0f, juce::Font::bold);
        inline const juce::Font microFont_1_00  = makeUiSans (9.0f,  juce::Font::plain);
        inline const juce::Font microFont_2_00  = makeUiSans (9.0f,  juce::Font::plain);

        inline const juce::Font& titleFont (float scaleKey)
        {
            return (scaleKey >= 1.75f) ? titleFont_2_00 : titleFont_1_00;
        }

        inline const juce::Font& headerFont (float scaleKey)
        {
            return (scaleKey >= 1.75f) ? headerFont_2_00 : headerFont_1_00;
        }

        inline const juce::Font& microFont (float scaleKey)
        {
            return (scaleKey >= 1.75f) ? microFont_2_00 : microFont_1_00;
        }
    }

    // ===== Phase 2: Meter Discrete Ladder =====
    namespace MeterLadder
    {
        inline float dotSizeMin (float scaleKey)
        {
            if (scaleKey >= 1.75f) return 2.5f;  // 2.00
            return 2.5f;  // 1.00
        }

        inline float dotSizeMax (float scaleKey)
        {
            if (scaleKey >= 1.75f) return 7.0f;  // 2.00
            return 7.0f;  // 1.00
        }

        inline float dotGapMin (float scaleKey)
        {
            if (scaleKey >= 1.75f) return 1.0f;  // 2.00
            return 1.0f;  // 1.00
        }
    }

    // ===== Knob rendering =====
    namespace Knob
    {
        // Ring alphas (constitution compliant)
        constexpr float outerRimAlpha = 0.70f;
        constexpr float lipHighlightAlpha = 0.60f;
        constexpr float innerShadowAlpha = 0.60f;
        constexpr float indicatorUnderStrokeAlpha = 0.45f;

        // Occlusion (must be <= occlusionAlphaMax)
        constexpr float occlusionAlpha = 0.18f;  // Reduced from 0.75 to comply

        // Radius multipliers
        constexpr float lipRadiusMultiplier = 0.96f;
        constexpr float innerShadowRadiusMultiplier = 0.92f;
        constexpr float indicatorLengthMultiplier = 0.48f;
        constexpr float indicatorStartRadiusMultiplier = 0.25f;
        constexpr float occlusionTopOffset = -0.5f;
        constexpr float occlusionBottomOffset = 0.7f;

        // Phase 2: Discrete stroke ladder by scaleKey (not radius)
        inline float getOuterRimThickness (float scaleKey)
        {
            return StrokeLadder::ringStrokeOuter (scaleKey);
        }

        inline float getLipThickness (float scaleKey)
        {
            return StrokeLadder::ringStrokeLip (scaleKey);
        }

        inline float getInnerShadowThickness (float scaleKey)
        {
            return StrokeLadder::ringStrokeInner (scaleKey);
        }

        inline float getIndicatorThickness (float scaleKey)
        {
            return StrokeLadder::indicatorStroke (scaleKey);
        }

        inline float getIndicatorUnderStrokeThickness (float scaleKey)
        {
            return StrokeLadder::indicatorUnderStroke (scaleKey);
        }
    }
}

