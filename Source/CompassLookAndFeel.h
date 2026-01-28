#pragma once
#include <JuceHeader.h>
#include <unordered_map>

class CompassLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CompassLookAndFeel()
    {
        // Default font
        setDefaultSansSerifTypefaceName("Inter");

        // Default color palette for text boxes
        setColour(juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha(0.7f));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, juce::Colour(0xFFE6A532).withAlpha(0.4f));
    }

    // --- 1. The Knob Style (Limiter Geometry + EQ Color Support) ---
    void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height, float pos, float startAngle, float endAngle, juce::Slider &slider) override
    {
        // 1. Determine Color (Check if slider has a specific color assigned, otherwise default to Gold/Industrial)
        juce::Colour accentColor = slider.findColour(juce::Slider::rotarySliderFillColourId);
        if (accentColor.isTransparent()) accentColor = juce::Colour(0xFFE6A532); // Default Gold

        // Active-band amount (0..1): prefer editor-provided grouping amount, fallback to self-derived
        float bandAmt = (float) slider.getProperties().getWithDefault("bandAmt", -1.0f);

        if (bandAmt < 0.0f)
        {
            const double def = slider.getDoubleClickReturnValue();
            const double v   = slider.getValue();
            const double dev = std::abs(v - def);

            const auto range = slider.getRange();
            const double start = range.getStart();
            const double end   = range.getEnd();
            const double maxDev = std::max(std::abs(def - start), std::abs(end - def));

            bandAmt = (maxDev > 0.0) ? (float) juce::jlimit(0.0, 1.0, dev / maxDev) : 0.0f;
        }
        else
        {
            bandAmt = juce::jlimit(0.0f, 1.0f, bandAmt);
        }

        const bool bandActive = bandAmt > 1.0e-6f;

        auto lift = [bandAmt](juce::Colour c)
        {
            // Subtle luminance lift that scales with how far the knob is from neutral
            const float t = 0.28f * bandAmt; // max lift
            return c.interpolatedWith(juce::Colours::white, t);
        };

        const juce::Colour accentLifted = lift(accentColor);

        // 2. Cache Key includes Size AND Color to support EQ bands
        const int sizeKey = (width << 16) | height;
        const int colorKey = (int)accentColor.getARGB();
        // Combine keys (simple hash)
        const uint64_t combinedKey = ((uint64_t)sizeKey << 32) | (uint64_t)colorKey;

        auto &bgImage = knobCache[combinedKey];

        // 3. Cache Generation
        if (bgImage.isNull() || bgImage.getWidth() != width || bgImage.getHeight() != height)
        {
            bgImage = juce::Image(juce::Image::ARGB, width, height, true);
            juce::Graphics g2(bgImage);

            auto bounds = juce::Rectangle<float>((float)width, (float)height);
            float side = juce::jmin((float)width, (float)height);
            auto center = bounds.getCentre();
            float r = (side * 0.5f) / 1.3f;

            // -- A. The Well (Recessed Background) --
            {
                float wellR = r * 1.15f;
                juce::ColourGradient well(juce::Colours::black.withAlpha(0.95f), center.x, center.y,
                                          juce::Colours::transparentBlack, center.x, center.y + wellR, true);
                well.addColour(r / wellR, juce::Colours::black.withAlpha(0.95f));
                g2.setGradientFill(well);
                g2.fillEllipse(center.x - wellR, center.y - wellR, wellR * 2, wellR * 2);
            }

            // -- B. Ticks --
            {
                int numTicks = 24;
                float tickR_Inner = r * 1.18f;
                float tickR_Outer_Major = r * 1.28f;
                float tickR_Outer_Minor = r * 1.23f;

                for (int i = 0; i <= numTicks; ++i)
                {
                    bool isMajor = (i % 4 == 0);
                    float angle = startAngle + (float)i / (float)numTicks * (endAngle - startAngle);
                    float outerR = isMajor ? tickR_Outer_Major : tickR_Outer_Minor;

                    // Tick Color: White for neutral, or slight tint of accent color for bands
                    g2.setColour(juce::Colours::white.withAlpha(isMajor ? 1.0f : 0.6f));

                    juce::Line<float> tick(center.getPointOnCircumference(tickR_Inner, angle),
                                           center.getPointOnCircumference(outerR, angle));
                    g2.drawLine(tick, isMajor ? 1.5f : 1.0f);
                }
            }

            // -- C. Main Body --
            float bodyR = r * 0.85f;
            g2.setGradientFill(juce::ColourGradient(juce::Colour(0xFF2B2B2B), center.x - bodyR, center.y - bodyR,
                                                    juce::Colour(0xFF050505), center.x + bodyR, center.y + bodyR, true));
            g2.fillEllipse(center.x - bodyR, center.y - bodyR, bodyR * 2, bodyR * 2);

            // -- D. Rim (Accented) --
            {
                // Mix white with accent color for the rim highlight
                juce::Colour rimCol = juce::Colours::white.interpolatedWith(accentColor, 0.4f);

                juce::ColourGradient rimGrad(rimCol.withAlpha(0.3f), center.x - bodyR, center.y - bodyR,
                                             juce::Colours::black, center.x + bodyR, center.y + bodyR, true);
                g2.setGradientFill(rimGrad);
                g2.drawEllipse(center.x - bodyR, center.y - bodyR, bodyR * 2, bodyR * 2, 2.0f);
            }

            // -- E. Face --
            float faceR = bodyR * 0.9f;
            g2.setGradientFill(juce::ColourGradient(juce::Colour(0xFF222222), center.x, center.y - faceR,
                                                    juce::Colour(0xFF0A0A0A), center.x, center.y + faceR, false));
            g2.fillEllipse(center.x - faceR, center.y - faceR, faceR * 2, faceR * 2);
        }

        // Draw Cached Background
        g.drawImageAt(bgImage, x, y);

        // Active-band hint (production): subtle ring that scales with distance from neutral
        if (bandActive)
        {
            auto boundsDyn = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height);
            const float sideDyn = juce::jmin(boundsDyn.getWidth(), boundsDyn.getHeight());
            const auto centerDyn = boundsDyn.getCentre();
            const float rDyn = (sideDyn * 0.5f) / 1.3f;
            const float bodyRDyn = rDyn * 0.85f;

            const float alpha = 0.06f + 0.34f * bandAmt;      // 0.06..0.40 (matches bypass “on” intensity)
            const float thick = 2.4f  + 1.8f  * bandAmt;      // 2.4..4.2

            g.setColour(juce::Colour(0xFFE6A532).withAlpha(alpha));
            g.drawEllipse(centerDyn.x - bodyRDyn, centerDyn.y - bodyRDyn, bodyRDyn * 2.0f, bodyRDyn * 2.0f, thick);
        }

        // Draw Dynamic Pointer (Rotates)
        auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height);
        float side = juce::jmin((float)width, (float)height);
        auto center = bounds.getCentre();
        float r = (side * 0.5f) / 1.3f;
        float bodyR = r * 0.85f;
        float faceR = bodyR * 0.9f;
        float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path p;
        float ptrW = 3.5f;
        float ptrLen = faceR * 0.6f;
        p.addRoundedRectangle(-ptrW * 0.5f, -faceR + 6.0f, ptrW, ptrLen, 1.0f);

        auto xf = juce::AffineTransform::rotation(angle).translated(center);

        // Pointer Color: White by default, or Tinted for bands (scales smoothly with bandAmt)
        g.setColour((bandActive ? accentLifted : accentColor)
                        .interpolatedWith(juce::Colours::white, 0.80f + 0.18f * bandAmt)
                        .withAlpha(0.90f + 0.10f * bandAmt));
        g.fillPath(p, xf);
    }

private:
    std::unordered_map<uint64_t, juce::Image> knobCache;
};
