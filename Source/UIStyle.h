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
    }

    // ===== Text alphas (paint hygiene ladder) =====
    namespace TextAlpha
    {
        constexpr float title = 0.90f;
        constexpr float header = 0.75f;
        constexpr float micro = 0.45f;
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

        // Stroke width ladder (scaffold for future scaleKey)
        struct StrokeWidthLadder
        {
            float outerRim40;
            float outerRim48;
            float outerRim56;
            float lip40;
            float lip48;
            float lip56;
            float innerShadow40;
            float innerShadow48;
            float innerShadow56;
            float indicator40;
            float indicator48;
            float indicator56;
        };

        inline StrokeWidthLadder getStrokeWidths()
        {
            return {
                2.0f, 2.4f, 2.8f,  // outer rim: 40/48/56
                1.2f, 1.4f, 1.6f,  // lip: 40/48/56
                1.2f, 1.4f, 1.6f,  // inner shadow: 40/48/56
                1.1f, 1.6f, 2.0f   // indicator: 40/48/56
            };
        }

        inline float getOuterRimThickness (float radius)
        {
            const auto ladder = getStrokeWidths();
            if (radius <= 20.0f)      return ladder.outerRim40;
            else if (radius <= 24.0f) return ladder.outerRim48;
            else                      return ladder.outerRim56;
        }

        inline float getLipThickness (float radius)
        {
            const auto ladder = getStrokeWidths();
            if (radius <= 20.0f)      return ladder.lip40;
            else if (radius <= 24.0f) return ladder.lip48;
            else                      return ladder.lip56;
        }

        inline float getInnerShadowThickness (float radius)
        {
            const auto ladder = getStrokeWidths();
            if (radius <= 20.0f)      return ladder.innerShadow40;
            else if (radius <= 24.0f) return ladder.innerShadow48;
            else                      return ladder.innerShadow56;
        }

        inline float getIndicatorThickness (float radius)
        {
            const auto ladder = getStrokeWidths();
            if (radius <= 20.0f)      return ladder.indicator40;
            else if (radius <= 24.0f) return ladder.indicator48;
            else                      return ladder.indicator56;
        }

        inline float getIndicatorUnderStrokeThickness (float indicatorThickness)
        {
            return indicatorThickness + 0.4f;
        }
    }
}

