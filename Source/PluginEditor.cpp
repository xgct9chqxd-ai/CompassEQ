#include "PluginEditor.h"
#include "Phase1Spec.h"
#include "UIStyle.h"

static constexpr int kEditorW = 760;
static constexpr int kEditorH = 460;

// ===== Phase 5.0 helper (cpp-only) =====
// ===== Phase 6.0 audit overlay (OFF by default) =====
static constexpr int kPaintAuditOverlay = 0;

namespace
{
    struct PlateStyle
    {
        float fillA      = 0.05f;
        float strokeA    = 0.12f;
        float strokeW    = 1.0f;
        float radius     = 6.0f;
        int   insetPx    = 0;   // optional: shrink rect for visual breathing
    };

    static inline void drawPlate (juce::Graphics& g, juce::Rectangle<int> r, PlateStyle s)
    {
        if (r.isEmpty())
            return;

        if (s.insetPx > 0)
            r = r.reduced (s.insetPx);

        const auto rf = r.toFloat();

        g.setColour (UIStyle::Colors::foreground.withAlpha (s.fillA));
        g.fillRoundedRectangle (rf, s.radius);

        g.setColour (UIStyle::Colors::foreground.withAlpha (s.strokeA));
        g.drawRoundedRectangle (rf, s.radius, s.strokeW);
    }

    static inline juce::Rectangle<int> fullWidthFrom (juce::Rectangle<int> editor, juce::Rectangle<int> zone, int inset)
    {
        // "Derived from slots": we take Y/H from the slot zone, and X/W from the editor bounds.
        if (zone.isEmpty() || editor.isEmpty())
            return {};

        auto r = juce::Rectangle<int> (editor.getX() + inset, zone.getY(), editor.getWidth() - (inset * 2), zone.getHeight());
        return r.getIntersection (editor);
    }

    // ===== Phase 8/9 Contract: Tier-2 faceplate baseline (flat, uniform) + Tier-3 wells =====
    // NOTE: Stage 1 ONLY: no zones, no seams, no gradients, no per-region faceplate logic.
    static inline juce::Colour gray8 (int v)
    {
        const uint8_t g = (uint8_t) juce::jlimit (0, 255, v);
        return juce::Colour::fromRGB (g, g, g);
    }

    static juce::Image createMatteNoiseTexture (int size = 512)
    {
        juce::Image noise (juce::Image::ARGB, size, size, true);
        juce::Graphics ng (noise);
        ng.fillAll (juce::Colours::black);

        juce::Random rnd (0x9f3c7a2b); // fixed seed for consistency

        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                // Very soft Perlin-like noise with horizontal directionality (brushed feel)
                float n = 0.0f;
                float amp = 1.0f;
                float freqX = 0.005f; // lower freq horizontal for streaks
                float freqY = 0.03f;  // higher freq vertical for subtle grain

                for (int oct = 0; oct < 5; ++oct) // add octave for depth
                {
                    float nx = amp * (rnd.nextFloat() * 2.0f - 1.0f)
                             * std::sin ((float) x * freqX + (float) y * freqY * 0.2f);

                    float ny = amp * (rnd.nextFloat() * 0.5f - 0.25f)
                             * std::cos ((float) y * freqY + (float) x * freqX * 0.1f);

                    // Stronger horizontal bias for brushed feel
                    nx *= 1.5f;
                    ny *= 0.5f;
                    n += nx + ny * 0.3f; // bias horizontal

                    amp *= 0.4f;
                    freqX *= 2.2f;
                    freqY *= 1.8f;
                }

                n = (n + 1.5f) * 0.4f; // adjust range for low contrast
                const uint8_t v = (uint8_t) juce::jlimit (0, 255, (int) (128.0f + n * 32.0f)); // ~12% deviation around mid-gray
                noise.setPixelAt (x, y, juce::Colour (v, v, v));
            }
        }

        return noise;
    }

    static const juce::Image matteNoise = createMatteNoiseTexture();

    // ===== SECTION BACKGROUND CONTRACT — STAGE 1 (LOCKED, NO VISUAL APPLICATION) =====
    // Baseline reference (Tier-2 fill region lock):
    //   screenshot "Screenshot 2025-12-22 at 4.01.06 PM.png"
    //
    // Color space lock: OKLab
    // Transfer space lock: linear light
    //
    // Transform rules (LOCKED):
    //   - Hue = knob hue (0° shift)
    //   - Saturation = knob saturation × satRatio, with saturation := OKLCH chroma C
    //   - Luminance = knob luminance − fixed offset, with luminance := OKLab L (scaled 0..100 for ΔL* values)
    //
    // Clamps (LOCKED):
    //   - Background chroma C <= 0.30
    //   - Background luminance delta in [-8, -14] => here fixed at -10
    //
    // NOTE: This stage defines the transform only. Do not apply any visuals/dividers here.
    struct OKLab { float L, a, b; }; // L in [0..1] (linear-light OKLab)

    static inline float srgbToLinear1 (float s)
    {
        // Explicit sRGB EOTF (gamma-encoded -> linear light)
        if (s <= 0.04045f) return s / 12.92f;
        return std::pow ((s + 0.055f) / 1.055f, 2.4f);
    }

    static inline float linearToSrgb1 (float l)
    {
        // Explicit sRGB OETF (linear light -> gamma-encoded)
        if (l <= 0.0031308f) return l * 12.92f;
        return 1.055f * std::pow (l, 1.0f / 2.4f) - 0.055f;
    }

    static inline OKLab linearSrgbToOklab (float rLin, float gLin, float bLin)
    {
        // Explicit linear-sRGB -> OKLab (Björn Ottosson)
        const float l = 0.4122214708f * rLin + 0.5363325363f * gLin + 0.0514459929f * bLin;
        const float m = 0.2119034982f * rLin + 0.6806995451f * gLin + 0.1073969566f * bLin;
        const float s = 0.0883024619f * rLin + 0.2817188376f * gLin + 0.6299787005f * bLin;

        const float l_ = std::cbrt (l);
        const float m_ = std::cbrt (m);
        const float s_ = std::cbrt (s);

        OKLab out;
        out.L = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
        out.a = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
        out.b = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;
        return out;
    }

    static inline void oklabToLinearSrgb (OKLab lab, float& rLin, float& gLin, float& bLin)
    {
        // Explicit OKLab -> linear-sRGB (Björn Ottosson)
        const float l_ = lab.L + 0.3963377774f * lab.a + 0.2158037573f * lab.b;
        const float m_ = lab.L - 0.1055613458f * lab.a - 0.0638541728f * lab.b;
        const float s_ = lab.L - 0.0894841775f * lab.a - 1.2914855480f * lab.b;

        const float l = l_ * l_ * l_;
        const float m = m_ * m_ * m_;
        const float s = s_ * s_ * s_;

        rLin = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
        gLin = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
        bLin = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
    }

    static inline juce::Colour stage1_knobToSectionBg_OkLabLinear (juce::Colour knobBaseSrgb)
    {
        // Locked numeric constants
        constexpr float satRatio = 0.30f;
        constexpr float maxChromaC = 0.30f;
        constexpr float luminanceDeltaLstar = -10.0f; // OKLab L scaled 0..100 for ΔL* values

        // Explicit transfer: sRGB (gamma) -> linear
        const float rLin = srgbToLinear1 (knobBaseSrgb.getFloatRed());
        const float gLin = srgbToLinear1 (knobBaseSrgb.getFloatGreen());
        const float bLin = srgbToLinear1 (knobBaseSrgb.getFloatBlue());

        // Explicit OKLab conversion
        OKLab lab = linearSrgbToOklab (rLin, gLin, bLin);

        // OKLCH (saturation := chroma C), hue preserved
        const float hue = std::atan2 (lab.b, lab.a);
        const float chroma = std::sqrt (lab.a * lab.a + lab.b * lab.b);

        const float newChroma = juce::jmin (maxChromaC, chroma * satRatio);

        // Luminance = OKLab L component, scaled 0..100 for the ΔL* rule.
        const float L100 = lab.L * 100.0f;
        const float newL100 = juce::jlimit (0.0f, 100.0f, L100 + luminanceDeltaLstar);
        lab.L = newL100 / 100.0f;

        lab.a = std::cos (hue) * newChroma;
        lab.b = std::sin (hue) * newChroma;

        // Explicit OKLab -> linear sRGB
        float outRLin = 0.0f, outGLin = 0.0f, outBLin = 0.0f;
        oklabToLinearSrgb (lab, outRLin, outGLin, outBLin);

        // Explicit transfer: linear -> sRGB (gamma), clamp to gamut
        const float outR = juce::jlimit (0.0f, 1.0f, linearToSrgb1 (juce::jlimit (0.0f, 1.0f, outRLin)));
        const float outG = juce::jlimit (0.0f, 1.0f, linearToSrgb1 (juce::jlimit (0.0f, 1.0f, outGLin)));
        const float outB = juce::jlimit (0.0f, 1.0f, linearToSrgb1 (juce::jlimit (0.0f, 1.0f, outBLin)));

        return juce::Colour::fromFloatRGBA (outR, outG, outB, 1.0f);
    }

    // Stage 1 knob source lock: use knob base color constant (sole source).
    static inline juce::Colour stage1_sectionBgFromKnobBase()
    {
        return stage1_knobToSectionBg_OkLabLinear (UIStyle::Colors::knobBody);
    }

    // ===== Stage 5.1 (LMF identity fill) — hue source lock revised =====
    // LOCK: BAND_HUE_CONSTANTS_ALLOWED: YES
    // LOCK: KNOB_RENDERING_MUST_REMAIN_NEUTRAL: YES
    // Hue source: UIStyle::Colors::bandHue* (OKLCH hue degrees), NOT knobBody.
    //
    // Numeric locks (unchanged):
    //   satRatio = 0.30
    //   ΔL* (OKLab L in 0..100 space) = -10
    //   C clamp <= 0.30
    //   opacity = 0.14
    static inline juce::Colour stage5_bandHueToSectionBg_OkLabLinear (float hueDeg, juce::Colour knobBodySrgbNeutral)
    {
        // PHASE 1 — COLOR & SATURATION REBALANCE (band background fills only)
        // Goal: unmistakable band identity (peripheral vision / squint), without touching layout/knobs/dividers.
        constexpr float satRatio = 0.85f;      // stronger chroma (primary lever)
        constexpr float maxChromaC = 0.38f;    // allow more chroma before clamp
        constexpr float luminanceDeltaLstar = -10.0f; // keep overall lane value in the existing dark range

        // Luminance source remains the knob body constant (neutral), per locked rules.
        const float kR = srgbToLinear1 (knobBodySrgbNeutral.getFloatRed());
        const float kG = srgbToLinear1 (knobBodySrgbNeutral.getFloatGreen());
        const float kB = srgbToLinear1 (knobBodySrgbNeutral.getFloatBlue());
        OKLab knobLab = linearSrgbToOklab (kR, kG, kB);

        const float L100 = knobLab.L * 100.0f;
        // Slight luminance separation between bands (subtle; saturation does the heavy lifting).
        // These are ΔL* in OKLab-L* (0..100) space, applied only to band lanes.
        float bandDeltaLstar = 0.0f;
        if (hueDeg == UIStyle::Colors::bandHueLF)      bandDeltaLstar = -2.0f; // LF anchor: heavier / quieter
        else if (hueDeg == UIStyle::Colors::bandHueLMF) bandDeltaLstar = +0.5f;
        else if (hueDeg == UIStyle::Colors::bandHueHMF) bandDeltaLstar =  0.0f;
        else if (hueDeg == UIStyle::Colors::bandHueHF)  bandDeltaLstar = +1.0f;

        const float newL100 = juce::jlimit (0.0f, 100.0f, L100 + luminanceDeltaLstar + bandDeltaLstar);

        // Hue is locked from band constants (OKLCH hue degrees). "Saturation" is OKLCH chroma C.
        // Since hue constants are angle-only, define source chroma as unit (1.0) in OKLab space for the satRatio rule.
        const float hueRad = juce::degreesToRadians (hueDeg);
        const float newChroma = (hueDeg < 0.0f) ? 0.0f : juce::jmin (maxChromaC, 1.0f * satRatio);

        OKLab lab;
        lab.L = newL100 / 100.0f;
        lab.a = std::cos (hueRad) * newChroma;
        lab.b = std::sin (hueRad) * newChroma;

        float outRLin = 0.0f, outGLin = 0.0f, outBLin = 0.0f;
        oklabToLinearSrgb (lab, outRLin, outGLin, outBLin);

        const float outR = juce::jlimit (0.0f, 1.0f, linearToSrgb1 (juce::jlimit (0.0f, 1.0f, outRLin)));
        const float outG = juce::jlimit (0.0f, 1.0f, linearToSrgb1 (juce::jlimit (0.0f, 1.0f, outGLin)));
        const float outB = juce::jlimit (0.0f, 1.0f, linearToSrgb1 (juce::jlimit (0.0f, 1.0f, outBLin)));

        return juce::Colour::fromFloatRGBA (outR, outG, outB, 1.0f);
    }

    // Stage 3: L* -> grayscale mapping (monotonic, grayscale only).
    // Note: We only need consistency + monotonicity for subtle ΔL* zoning (no hue/sat/texture).
    static inline juce::Colour lstarToGray (float lstar)
    {
        const float clamped = juce::jlimit (0.0f, 100.0f, lstar);
        const int gray = juce::roundToInt (clamped * 2.55f); // linear 0..100 -> 0..255
        return gray8 (gray);
    }

    // STAGE 2 — LIGHTING INVARIANCE LOCK (Tier 2 only)
    // One lighting function. One highlight behavior. One occlusion behavior.
    // Apply uniformly to any Tier-2 faceplate geometry. Do not introduce per-zone lighting.
    static void applyTier2LightingUniform (juce::Graphics& g, juce::Rectangle<int> r, float physicalScale)
    {
        if (r.isEmpty())
            return;

        const float px = juce::jmax (1.0f, 1.0f / physicalScale);
        const float x1 = UIStyle::Snap::snapPx ((float) r.getX(), physicalScale);
        const float y1 = UIStyle::Snap::snapPx ((float) r.getY(), physicalScale);
        const float x2 = UIStyle::Snap::snapPx ((float) r.getRight(), physicalScale);
        const float y2 = UIStyle::Snap::snapPx ((float) r.getBottom(), physicalScale);

        // Highlight (top + left) — capped
        g.setColour (juce::Colours::white.withAlpha (juce::jmin (UIStyle::highlightAlphaMax, 0.12f)));
        g.drawLine (x1, y1, x2, y1, px);
        g.drawLine (x1, y1, x1, y2, px);

        // Occlusion (bottom + right) — capped
        g.setColour (juce::Colours::black.withAlpha (juce::jmin (UIStyle::occlusionAlphaMax, 0.18f)));
        g.drawLine (x1, y2, x2, y2, px);
        g.drawLine (x2, y1, x2, y2, px);
    }

    // STAGE 3 — ZONE VALUE DELTAS (NO SEAMS)
    // Value-only zone fills (no borders/lines/seams), then one uniform lighting pass over the full plate.
    static void drawFaceplateStage3ZonedNoSeams (
        juce::Graphics& g,
        juce::Rectangle<int> editor,
        juce::Rectangle<int> zoneHeader,
        juce::Rectangle<int> zoneFilters,
        juce::Rectangle<int> zoneBands,
        juce::Rectangle<int> zoneTrim,
        juce::Rectangle<int> colLF,
        juce::Rectangle<int> colLMF,
        juce::Rectangle<int> colHMF,
        juce::Rectangle<int> colHF,
        juce::Rectangle<int> lfFreqKnob,
        juce::Rectangle<int> lmfFreqKnob,
        juce::Rectangle<int> hmfFreqKnob,
        juce::Rectangle<int> hfFreqKnob,
        juce::Rectangle<int> hpfKnob,
        juce::Rectangle<int> lpfKnob,
        juce::Rectangle<int> meterInRect,
        juce::Rectangle<int> meterOutRect,
        float physicalScale)
    {
        if (editor.isEmpty())
            return;

        // ===== Waves SSL-inspired console panel (base) =====
        // Brushed metal background gradient (dark grey -> warm medium grey) + faint horizontal brush lines.
        {
            const auto b = editor.toFloat();
            juce::ColourGradient metalGrad (juce::Colour (0xFF303030), b.getX(), b.getY(),
                                            juce::Colour (0xFF404040), b.getX(), b.getBottom(),
                                            false);
            g.setGradientFill (metalGrad);
            g.fillRect (b);

            g.setColour (juce::Colours::white.withAlpha (0.03f));
            for (int y = editor.getY(); y < editor.getBottom(); y += 3)
                g.drawLine ((float) editor.getX(), (float) y, (float) editor.getRight(), (float) y, 0.5f);

            // Subtle matte noise overlay on faceplate only
            g.setTiledImageFill (matteNoise, 0, 0, 0.05f); // opacity 5% (within 2-6% spec)
            g.fillRect (editor); // editor = full bounds

            // Ultra-subtle vignette (lens/light falloff on the physical plate), under everything
            {
                juce::ColourGradient vignette (juce::Colours::transparentBlack, b.getCentreX(), b.getCentreY(),
                                               juce::Colours::black.withAlpha (0.08f), b.getCentreX(), b.getCentreY(),
                                               true); // radial
                vignette.multiplyOpacity (0.05f); // shallow
                g.setGradientFill (vignette);
                g.fillRect (b);
            }
        }

        // STAGE 5 — ROLL-OUT TO ALL EQ BANDS (LF / LMF / HMF / HF)
        // LOCKED CONSTANTS (DO NOT CHANGE):
        //   - satRatio = 0.55f
        //   - ΔL* = −10
        //   - C clamp ≤ 0.30
        //   - opacity = 0.14
        //   - fill geometry = equal-width lanes (Stage 5.7 lock)
        //   - draw order = NO_POST_DIMMING
        // Dividers (LOCKED):
        //   - thickness = 1px
        //   - color = RGB(230,230,230)
        //   - opacity = 0.65f
        //   - position = computed divider x's between equal-width lanes only
        {
            auto bandsRect = zoneBands.getIntersection (editor);
            if (! bandsRect.isEmpty())
            {
                // STAGE 5.7 — EQUAL-WIDTH BAND LANES (LOCKED)
                // STAGE 5.8 — EXPAND BAND SPAN TO METERS (LOCKED)
                // Use meter bounds, keep a fixed gap.
                constexpr float meterGap = 10.0f; // LOCKED
                const float bandsSpanLeft  = (float) meterInRect.getRight() + meterGap;
                const float bandsSpanRight = (float) meterOutRect.getX() - meterGap;
                const float bandsSpanW = bandsSpanRight - bandsSpanLeft;

                constexpr float dividerW = 1.0f;
                const float laneW = (bandsSpanW - 3.0f * dividerW) / 4.0f;

                const float x0 = bandsSpanLeft;
                const float xDiv1 = x0 + laneW;
                const float xDiv2 = x0 + 2.0f * laneW + 1.0f * dividerW;
                const float xDiv3 = x0 + 3.0f * laneW + 2.0f * dividerW;

                // Shrink the TOP of the colored lanes so it starts at the top of the FREQ (KHz) knob.
                // Do not change knob positions; do not change the lane bottom.
                constexpr float kLaneTopExtra = 18.0f;
                const int yTopKHz = juce::jmin (lfFreqKnob.getY(), lmfFreqKnob.getY(),
                                               hmfFreqKnob.getY(), hfFreqKnob.getY());
                const float y1 = (float) yTopKHz - kLaneTopExtra; // raise lane top slightly above the KHz knob
                const float y2 = (float) bandsRect.getBottom() + 6.0f;

                // Soft band panels (colored metal tints; no hard blocks)
                auto drawBandPanel = [&] (juce::Rectangle<float> lane, juce::Colour cTop, juce::Colour cBot)
                {
                    auto panel = lane.reduced (6.0f, 8.0f);
                    if (panel.isEmpty())
                        return;

                    // Phase 2B (Option 2): alpha ×0.2 AND shallower contrast (pull top/bottom closer)
                    const auto tTop = cTop.withMultipliedAlpha (0.20f);
                    const auto tBot = cBot.withMultipliedAlpha (0.20f);
                    const auto mid = tTop.interpolatedWith (tBot, 0.4f); // pull toward darker

                    juce::ColourGradient grad (mid, panel.getCentreX(), panel.getY(),
                                               tBot, panel.getCentreX(), panel.getBottom(),
                                               false);
                    g.setGradientFill (grad);
                    g.fillRoundedRectangle (panel, 12.0f);

                    // Thin engraved borders (silver outer + inner shadow)
                    g.setColour (juce::Colours::silver.withAlpha (0.30f));
                    g.drawRoundedRectangle (panel, 12.0f, 1.5f);
                    g.setColour (juce::Colours::black.withAlpha (0.50f));
                    g.drawRoundedRectangle (panel.reduced (0.5f), 12.0f, 0.8f);
                };

                const auto laneLF  = juce::Rectangle<float> (x0,                             y1, laneW, y2 - y1);
                const auto laneLMF = juce::Rectangle<float> (x0 + laneW + dividerW,          y1, laneW, y2 - y1);
                const auto laneHMF = juce::Rectangle<float> (x0 + 2.0f * laneW + 2*dividerW, y1, laneW, y2 - y1);
                const auto laneHF  = juce::Rectangle<float> (x0 + 3.0f * laneW + 3*dividerW, y1, laneW, y2 - y1);

                // Soft tinted gradients per band (subtle "colored metal")
                drawBandPanel (laneLF,
                               juce::Colours::blue.withAlpha (0.18f),
                               juce::Colours::darkblue.withAlpha (0.09f));
                drawBandPanel (laneLMF,
                               juce::Colours::purple.withAlpha (0.15f),
                               juce::Colours::darkslateblue.withAlpha (0.08f));
                drawBandPanel (laneHMF,
                               juce::Colours::forestgreen.withAlpha (0.12f),
                               juce::Colours::darkgreen.withAlpha (0.06f));
                drawBandPanel (laneHF,
                               juce::Colours::darkred.withAlpha (0.21f),
                               juce::Colours::maroon.withAlpha (0.11f));

                // Etched dividers between bands (SSL strip separation)
                g.setColour (juce::Colours::lightgrey.withAlpha (0.20f));
                g.drawLine (xDiv1, y1, xDiv1, y2, 1.2f);
                g.drawLine (xDiv2, y1, xDiv2, y2, 1.2f);
                g.drawLine (xDiv3, y1, xDiv3, y2, 1.2f);
            }
        }

        // Stage 5.5 lock: no alpha overlays that re-lighten the neutral black base afterward.

        // STAGE 5.4b — TOP FILTER ZONE = NEUTRAL BLACK (SSL-LIKE) (LOCKED)
        // A) Replace filter bar fill with neutral black (no tint/no hue/no noise).
        // B) HPF/LPF placement is computed from this rect in resized(): centerY = y + (h * 0.40).
        // C) Ensure no residual grey remains: this rect is drawn last among plate elements (after lighting) but before knobs.
        {
            // Locked: use exact neutral background tone already used (no alpha blending against plate).
            const auto topZoneColor = juce::Colour::fromRGB (18, 18, 18);
            constexpr int padTop = 10;
            constexpr int padBottom = 10;

            // X bounds: same lane span as band lanes (LF lane left -> HF lane right)
            const auto bandsRect = zoneBands.getIntersection (editor);
            const int xLeft_LF   = bandsRect.getX();
            const int xRight_HF  = bandsRect.getRight();

            // Y bounds: defined from HPF/LPF label-top area down to knob block bottom, plus locked padding.
            // Use the knob bounds (already centered in resized()) only to locate the vertical extent, not to size via union-of-controls.
            constexpr int labelTopPad = 28; // header yOffset=-16, height=12 => 28px above knob top
            const int knobTop = juce::jmin (hpfKnob.getY(), lpfKnob.getY());
            const int knobBottom = juce::jmax (hpfKnob.getBottom(), lpfKnob.getBottom());
            const int yTop = (knobTop - labelTopPad) - padTop;
            const int yBottom = knobBottom + padBottom;

            auto filterBar = juce::Rectangle<int> (xLeft_LF, yTop, xRight_HF - xLeft_LF, yBottom - yTop)
                                 .getIntersection (editor);

            if (! filterBar.isEmpty())
            {
                // Console metal base already covers this region; no separate black bar in this aesthetic.
                juce::ignoreUnused (topZoneColor, filterBar);
            }
        }
    }

    static void drawTier3Well (juce::Graphics& g, juce::Rectangle<int> knobBounds, float physicalScale)
    {
        if (knobBounds.isEmpty())
            return;

        // Tier 3: recessed well behind the knob (no gradients, no panels)
        const auto wellCol = gray8 (26);
        const float px = juce::jmax (1.0f, 1.0f / physicalScale);

        const auto outer = knobBounds.expanded (6, 6).toFloat();
        g.setColour (wellCol);
        g.fillEllipse (outer);

        // Inner lip edge grammar (one highlight arc + one occlusion arc)
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addEllipse (outer);
        g.reduceClipRegion (clip);

        const auto c = outer.getCentre();
        const float r = outer.getWidth() * 0.5f - px * 0.75f;

        auto strokeArcDeg = [&] (float degStart, float degEnd, juce::Colour col, float alpha)
        {
            const float a0 = juce::degreesToRadians (degStart);
            const float a1 = juce::degreesToRadians (degEnd);
            juce::Path p;
            p.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a1, true);
            g.setColour (col.withAlpha (alpha));
            g.strokePath (p, juce::PathStrokeType (px, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        };

        strokeArcDeg (175.0f, 265.0f, juce::Colours::white, juce::jmin (UIStyle::highlightAlphaMax, 0.12f));
        strokeArcDeg ( -5.0f,  85.0f, juce::Colours::black, juce::jmin (UIStyle::occlusionAlphaMax, 0.18f));
    }

    static void drawBandWell (juce::Graphics& g, juce::Rectangle<int> columnRect, float physicalScale)
    {
        if (columnRect.isEmpty()) return;

        // Keep existing reduction for breathing room
        auto well = columnRect.toFloat().reduced (8.0f, 12.0f);

        if (well.isEmpty()) return;

        // 1. Recessed fill — keep dark
        g.setColour (gray8 (26));
        g.fillRoundedRectangle (well, 12.0f);

        // 2. Rim stroke — 1px very dark (exact #111)
        g.setColour (juce::Colour (0xFF111111));
        g.drawRoundedRectangle (well, 12.0f, 1.0f);

        // 3. Inner shadow — tighter and stronger for machined depth
        //    Opacity 28% (within 20-30% spec), blur 5px (within 4-8px), downward offset 3px
        juce::DropShadow shadow (juce::Colours::black.withAlpha (0.28f), 5, { 0, 3 });
        shadow.drawForRectangle (g, well.getSmallestIntegerContainer());

        // 4. Top edge highlight — sharper and only on absolute top
        //    1.5px white @12% opacity (within 1-2px / ~10% spec), top horizontal only
        const float px = juce::jmax (1.0f, 1.0f / physicalScale);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawLine (well.getX(), well.getY(), well.getX() + well.getWidth(), well.getY(), 1.5f * px);

        // Optional subtle bottom darkening to enhance recess (very light, no new elements)
        g.setColour (juce::Colours::black.withAlpha (0.08f));
        g.drawLine (well.getX(), well.getBottom() - px, well.getX() + well.getWidth(), well.getBottom() - px, 1.0f * px);
    }

    // ===== Waves SSL-style knobs (vector; no assets) =====
    // Cached base knob rendering (no pointer), drawn + pointer line on top per-frame.
    // This intentionally uses gradients to achieve the classic SSL bevel/metallic depth.
    //
    // Note: This supersedes the prior deterministic-flat knob system for this change set.
    static float gRotaryStartAngleRad = 0.0f;
    static float gRotaryEndAngleRad   = juce::MathConstants<float>::twoPi;

    struct WavesSSLKnobCache
    {
        static juce::Image render (int sizePx, juce::Colour bandColour)
        {
            juce::Image img (juce::Image::ARGB, sizePx, sizePx, true);
            juce::Graphics gg (img);

            const auto s = (float) sizePx;
            auto bounds = juce::Rectangle<float> (0.0f, 0.0f, s, s).reduced (1.0f);
            const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const float cx = bounds.getCentreX();
            const float cy = bounds.getCentreY();

            // 0) Drop shadow (keep for depth)
            {
                juce::Path p;
                p.addEllipse (bounds.reduced (2.0f));
                const juce::DropShadow shadow (juce::Colours::black.withAlpha (0.40f), 6, { 0, 3 });
                shadow.drawForPath (gg, p);
            }

            // 1) Outer chrome rim (keep neutral)
            {
                juce::ColourGradient rimGrad (juce::Colours::whitesmoke, cx, cy - radius * 0.80f,
                                              juce::Colours::darkgrey, cx, cy + radius * 0.80f,
                                              false);
                gg.setGradientFill (rimGrad);
                gg.fillEllipse (bounds);
            }

            // 2) Band accent ring + neutral body (band knobs only; filters/trim stay neutral via bandColour input)
            auto knobBounds = bounds.reduced (juce::jmax (6.0f, radius * 0.18f));
            {
                const auto ringBounds = knobBounds.reduced (4.0f);
                const auto bodyBounds = knobBounds.reduced (6.0f);

                // Thin colored ring (accent)
                if (! ringBounds.isEmpty())
                {
                    const auto ringBase = bandColour;
                    juce::ColourGradient ringGrad (ringBase.brighter (0.08f), cx, cy - radius * 0.35f,
                                                   ringBase.darker (0.10f),  cx, cy + radius * 0.35f,
                                                   false);
                    gg.setGradientFill (ringGrad);
                    gg.fillEllipse (ringBounds);
                }

                // Neutral body (matte hardware)
                if (! bodyBounds.isEmpty())
                {
                    const auto bodyBase = gray8 (34);
                    juce::ColourGradient bodyGrad (bodyBase.brighter (0.10f), cx, cy - radius * 0.45f,
                                                   bodyBase.darker (0.18f),  cx, cy + radius * 0.35f,
                                                   false);
                    gg.setGradientFill (bodyGrad);
                    gg.fillEllipse (bodyBounds);
                }
            }

            // 3) Gloss stronger for plastic shine
            {
                juce::Path highlight;
                highlight.addEllipse (knobBounds.reduced (5.0f));
                gg.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), cx, cy - radius * 0.70f,
                                                          juce::Colours::transparentWhite,       cx, cy + radius * 0.10f,
                                                          false));
                gg.fillPath (highlight);
            }

            // 3b) Subtle rim highlight (cap edge catch-light; top-left biased)
            {
                juce::ColourGradient rimHigh (juce::Colours::white.withAlpha (0.12f), cx, cy - radius * 0.6f,
                                              juce::Colours::transparentWhite,       cx, cy + radius * 0.2f,
                                              false);
                gg.setGradientFill (rimHigh);
                gg.fillEllipse (knobBounds.reduced (2.0f));
            }

            // 4) Inner rim shadow (keep subtle)
            {
                gg.setColour (juce::Colours::black.withAlpha (0.35f));
                gg.drawEllipse (knobBounds, juce::jmax (1.0f, radius * 0.06f));
            }

            return img;
        }

        static const juce::Image& get (int sizePx, juce::Colour bandColour)
        {
            static std::map<std::pair<int, uint32_t>, juce::Image> cache;
            const auto key = std::make_pair (sizePx, (uint32_t) bandColour.getARGB());
            auto it = cache.find (key);
            if (it != cache.end())
                return it->second;
            cache[key] = render (sizePx, bandColour);
            return cache[key];
        }
    };

    static void drawSSLKnob (juce::Graphics& g, juce::Rectangle<float> b, float value01, float scaleKey, juce::Colour bandColour)
    {
        if (b.isEmpty())
            return;

        const float physicalScale = juce::jmax (1.0f, (float) g.getInternalContext().getPhysicalPixelScaleFactor());
        const float size = juce::jmin (b.getWidth(), b.getHeight());
        const int sizePx = juce::jlimit (24, 512, juce::roundToInt (size * physicalScale));

        const auto& base = WavesSSLKnobCache::get (sizePx, bandColour);
        auto dst = juce::Rectangle<float> (b.getCentreX() - size * 0.5f,
                                           b.getCentreY() - size * 0.5f,
                                           size, size);

        g.drawImage (base, dst, juce::RectanglePlacement::stretchToFit);

        // Pointer / indicator line (classic SSL: white line pointer)
        const float v = juce::jlimit (0.0f, 1.0f, value01);
        const float angleRad = gRotaryStartAngleRad + v * (gRotaryEndAngleRad - gRotaryStartAngleRad);

        const auto c = dst.getCentre();
        const float radius = dst.getWidth() * 0.5f;
        const float len = radius * 0.75f;
        const auto p1 = juce::Point<float> (c.x + std::cos (angleRad) * len,
                                            c.y + std::sin (angleRad) * len);

        // Pointer / indicator (bright, crisp, scale-aware). Draw a dark under-stroke for depth.
        const float w = juce::jmax (1.6f, 1.6f * scaleKey);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawLine (c.x, c.y, p1.x, p1.y, w + 0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.98f));
        g.drawLine (c.x, c.y, p1.x, p1.y, w);

        // Center hub (small chrome)
        g.setColour (juce::Colours::silver);
        g.fillEllipse (c.x - 4.0f, c.y - 4.0f, 8.0f, 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillEllipse (c.x - 2.0f, c.y - 2.0f, 4.0f, 4.0f);
    }
}

// ===== Value popup helper (cpp-only) =====
static inline juce::String popupTextFor (juce::Slider& s)
{
    const auto name = s.getName();
    const double value = s.getValue();

    // Frequency knobs (all Freq sliders)
    if (name.containsIgnoreCase ("frequency") || name.containsIgnoreCase ("freq"))
    {
        if (value >= 1000.0)
            return juce::String (value / 1000.0, 2) + " kHz";

        return juce::String (value, 2) + " Hz";
    }

    // Gain (GR)
    if (name.containsIgnoreCase ("gain") || name.containsIgnoreCase ("gr"))
        return juce::String (value, 1) + " dB";

    // Q
    if (name.containsIgnoreCase ("q"))
        return juce::String (value, 1);

    // Trim (fallback)
    if (name.containsIgnoreCase ("trim"))
        return juce::String (value, 1) + " dB";

    // Default fallback
    return s.getTextFromValue (value);
}

// ===== Custom LookAndFeel implementation =====
// Phase 3C: PERFORMANCE BUDGET VERIFICATION CHECKLIST
// ✓ No std::vector growth, no std::string concat, no Path churn that allocates
// ✓ No Image reallocation in paint, no Gradient objects created per-frame without reuse/caching
// ✓ Fonts from ladders/tokens (FontLadder returns const references)
// ✓ Path objects: knobClip, underStroke, indicatorPath are stack-allocated (acceptable for small paths)
// ✓ All coordinates are stack-allocated floats/ints
// ✓ Caches rebuilt only on invalidation events (scaleKey change, resize) via AsyncUpdater
void CompassEQAudioProcessorEditor::CompassLookAndFeel::drawRotarySlider (
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
    juce::Slider& s)
{
    const float scaleKey = editor.getScaleKeyActive();
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
    if (bounds.isEmpty())
        return;

    // Band colour mapping (corrected): LF blue, LMF purple, HMF green, HF red
    auto capColourForHue = [] (float hueDeg) -> juce::Colour
    {
        if (hueDeg < 0.0f)
            return juce::Colours::darkgrey.brighter (0.30f); // neutral for non-band

        // OKLab hue source + boost for solid cap look
        auto temp = stage5_bandHueToSectionBg_OkLabLinear (hueDeg, juce::Colours::darkgrey);
        return temp.withMultipliedSaturation (1.8f * 0.75f).brighter (0.20f); // desaturate 25%
    };

    juce::Colour bandColour = juce::Colours::darkgrey.brighter (0.30f);
    const auto nm = s.getName();
    if (nm.startsWith ("LF"))
        bandColour = capColourForHue (UIStyle::Colors::bandHueLF);
    else if (nm.startsWith ("LMF"))
        bandColour = capColourForHue (UIStyle::Colors::bandHueLMF);
    else if (nm.startsWith ("HMF"))
        bandColour = capColourForHue (UIStyle::Colors::bandHueHMF);
    else if (nm.startsWith ("HF"))
        bandColour = capColourForHue (UIStyle::Colors::bandHueHF);

    // Hover ring (kept)
    if (s.isMouseOverOrDragging())
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawEllipse (bounds.expanded (6.0f), 2.0f);
    }

    gRotaryStartAngleRad = rotaryStartAngle;
    gRotaryEndAngleRad = rotaryEndAngle;

    // Crisp indicators: request low-resampling before knob draw (affects image draw + subsequent line)
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    drawSSLKnob (g, bounds, sliderPos, scaleKey, bandColour);

    // Tight contact shadow (single, post-image; avoids cached DPI artifacts)
    {
        juce::DropShadow contact (juce::Colours::black.withAlpha (0.22f), 2, { 0, 2 });
        juce::Path p;
        p.addEllipse (bounds.reduced (2.0f));
        contact.drawForPath (g, p);
    }

    // Restore default-ish resampling quality for anything drawn after this
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
}

CompassEQAudioProcessorEditor::CompassEQAudioProcessorEditor (CompassEQAudioProcessor& p)
    : juce::AudioProcessorEditor (&p)
    , proc (p)
    , apvts (proc.getAPVTS())
    , inputMeter  (proc, true, *this)
    , outputMeter (proc, false, *this)
    , lookAndFeel (std::make_unique<CompassLookAndFeel>(*this))
    , valueReadout (*this)
{
    setResizable (false, false);
    setSize (kEditorW, kEditorH);

    // Phase 6: Configure knobs with parameter defaults for double-click reset
    using namespace phase1;
    configureKnob (lfFreq, LF_FREQUENCY_ID, Ranges::LF_FREQ_DEF);
    configureKnob (lfGain, LF_GAIN_ID, Ranges::GAIN_DEF);
    configureKnob (lmfFreq, LMF_FREQUENCY_ID, Ranges::LMF_FREQ_DEF);
    configureKnob (lmfGain, LMF_GAIN_ID, Ranges::GAIN_DEF);
    configureKnob (lmfQ, LMF_Q_ID, Ranges::Q_DEF);
    configureKnob (hmfFreq, HMF_FREQUENCY_ID, Ranges::HMF_FREQ_DEF);
    configureKnob (hmfGain, HMF_GAIN_ID, Ranges::GAIN_DEF);
    configureKnob (hmfQ, HMF_Q_ID, Ranges::Q_DEF);
    configureKnob (hfFreq, HF_FREQUENCY_ID, Ranges::HF_FREQ_DEF);
    configureKnob (hfGain, HF_GAIN_ID, Ranges::GAIN_DEF);
    configureKnob (hpfFreq, HPF_FREQUENCY_ID, Ranges::HPF_DEF);
    configureKnob (lpfFreq, LPF_FREQUENCY_ID, Ranges::LPF_DEF);
    configureKnob (inTrim, INPUT_TRIM_ID, Ranges::TRIM_DEF);
    configureKnob (outTrim, OUTPUT_TRIM_ID, Ranges::TRIM_DEF);

    // Internal names (no extra UI elements)
    lfFreq.setName ("LF Frequency");  lfGain.setName ("LF Gain");
    lmfFreq.setName ("LMF Frequency"); lmfGain.setName ("LMF Gain"); lmfQ.setName ("LMF Q");
    hmfFreq.setName ("HMF Frequency"); hmfGain.setName ("HMF Gain"); hmfQ.setName ("HMF Q");
    hfFreq.setName ("HF Frequency");  hfGain.setName ("HF Gain");

    hpfFreq.setName ("HPF Frequency");
    lpfFreq.setName ("LPF Frequency");

    inTrim.setName ("Input Trim");
    outTrim.setName ("Output Trim");

    // ===== Phase 6: Fixed Value Readout wiring =====
    // Readout is positioned in resized() with fixed bounds (no moving popup)
    // Helper: update readout text (allocation-safe via ValueReadout)
    auto updateReadout = [this] (CompassSlider& s)
    {
        if (activeSlider == &s)
        {
            valueReadout.setValueText (popupTextFor (s));
            valueReadout.show();
        }
    };

    // Wire drag-only popup readout behavior for each slider
    auto wireReadout = [this, updateReadout] (CompassSlider& s)
    {
        s.onDragStart = [this, updateReadout, &s]
        {
            activeSlider = &s;
            valueReadout.show();
            updateReadout (s);
        };

        s.onValueChange = [this, updateReadout, &s]
        {
            // Phase 6: Only update while actively dragging
            if (s.isMouseButtonDown())
            {
                updateReadout (s);
            }
        };

        s.onDragEnd = [this]
        {
            valueReadout.hide();
        activeSlider = nullptr;
    };
    };

    // Apply to all sliders
    wireReadout (lfFreq);
    wireReadout (lfGain);
    wireReadout (lmfFreq);
    wireReadout (lmfGain);
    wireReadout (lmfQ);
    wireReadout (hmfFreq);
    wireReadout (hmfGain);
    wireReadout (hmfQ);
    wireReadout (hfFreq);
    wireReadout (hfGain);
    wireReadout (hpfFreq);
    wireReadout (lpfFreq);
    wireReadout (inTrim);
    wireReadout (outTrim);


    globalBypass.setName ("Global Bypass");

    // Global bypass button (no hidden interactions)
    globalBypass.setButtonText ("BYPASS");
    globalBypass.setClickingTogglesState (true);

    globalBypass.onAltClick = [this]
    {
        proc.togglePureMode();

       #if JUCE_DEBUG
        DBG (juce::String ("[UI] Pure Mode = ") + (proc.getPureMode() ? "ON" : "OFF"));
       #endif
    };

    addAndMakeVisible (globalBypass);

    auto addKnob = [this] (juce::Slider& s) { addAndMakeVisible (s); };

    // Add sliders
    addKnob (lfFreq); addKnob (lfGain);
    addKnob (lmfFreq); addKnob (lmfGain); addKnob (lmfQ);
    addKnob (hmfFreq); addKnob (hmfGain); addKnob (hmfQ);
    addKnob (hfFreq);  addKnob (hfGain);

    addKnob (hpfFreq);
    addKnob (lpfFreq);

    addKnob (inTrim);
    addKnob (outTrim);

    // Meters (2 only)
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    // Value popup label (ensure it exists, is non-interactive, and stays above)
    // Phase 6: Add fixed value readout (positioned in resized() with fixed bounds)
    addAndMakeVisible (valueReadout);
    valueReadout.toFront (false);
    // Readout starts hidden (no text shown on plugin load)

    // Attachments using REAL IDs from Phase1Spec.h (namespace phase1)
    attLfFreq  = std::make_unique<SliderAttachment> (apvts, phase1::LF_FREQUENCY_ID,  lfFreq);
    attLfGain  = std::make_unique<SliderAttachment> (apvts, phase1::LF_GAIN_ID,       lfGain);

    attLmfFreq = std::make_unique<SliderAttachment> (apvts, phase1::LMF_FREQUENCY_ID, lmfFreq);
    attLmfGain = std::make_unique<SliderAttachment> (apvts, phase1::LMF_GAIN_ID,      lmfGain);
    attLmfQ    = std::make_unique<SliderAttachment> (apvts, phase1::LMF_Q_ID,         lmfQ);

    attHmfFreq = std::make_unique<SliderAttachment> (apvts, phase1::HMF_FREQUENCY_ID, hmfFreq);
    attHmfGain = std::make_unique<SliderAttachment> (apvts, phase1::HMF_GAIN_ID,      hmfGain);
    attHmfQ    = std::make_unique<SliderAttachment> (apvts, phase1::HMF_Q_ID,         hmfQ);

    attHfFreq  = std::make_unique<SliderAttachment> (apvts, phase1::HF_FREQUENCY_ID,  hfFreq);
    attHfGain  = std::make_unique<SliderAttachment> (apvts, phase1::HF_GAIN_ID,       hfGain);

    attHpfFreq = std::make_unique<SliderAttachment> (apvts, phase1::HPF_FREQUENCY_ID, hpfFreq);
    attLpfFreq = std::make_unique<SliderAttachment> (apvts, phase1::LPF_FREQUENCY_ID, lpfFreq);

    attInTrim  = std::make_unique<SliderAttachment> (apvts, phase1::INPUT_TRIM_ID,    inTrim);
    attOutTrim = std::make_unique<SliderAttachment> (apvts, phase1::OUTPUT_TRIM_ID,   outTrim);

    attBypass  = std::make_unique<ButtonAttachment> (apvts, phase1::GLOBAL_BYPASS_ID, globalBypass);
}

CompassEQAudioProcessorEditor::~CompassEQAudioProcessorEditor()
{
    // Phase 4: Set teardown flag first to prevent AsyncUpdater callbacks
    isTearingDown = true;
    
    // Phase 4: Cancel any pending AsyncUpdater callbacks
    cancelPendingUpdate();
    
    // Phase 4: Clear LookAndFeel from all sliders to prevent crash on destruction
    lfFreq.setLookAndFeel (nullptr);
    lfGain.setLookAndFeel (nullptr);
    lmfFreq.setLookAndFeel (nullptr);
    lmfGain.setLookAndFeel (nullptr);
    lmfQ.setLookAndFeel (nullptr);
    hmfFreq.setLookAndFeel (nullptr);
    hmfGain.setLookAndFeel (nullptr);
    hmfQ.setLookAndFeel (nullptr);
    hfFreq.setLookAndFeel (nullptr);
    hfGain.setLookAndFeel (nullptr);
    hpfFreq.setLookAndFeel (nullptr);
    lpfFreq.setLookAndFeel (nullptr);
    inTrim.setLookAndFeel (nullptr);
    outTrim.setLookAndFeel (nullptr);
    
    // Phase 4: lookAndFeel unique_ptr will be destroyed here (after all components cleared)
}

void CompassEQAudioProcessorEditor::configureKnob (CompassSlider& s, const char* paramId, float defaultValue)
{
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    // Correct standard pro EQ arc: min ~8 o'clock, neutral 12 o'clock straight up, max ~4 o'clock
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.5f - juce::MathConstants<float>::pi * 0.833f,   // start ~225° (8 o'clock min)
                           juce::MathConstants<float>::pi * 1.5f + juce::MathConstants<float>::pi * 0.833f,   // end ~315° (4 o'clock max)
                           true);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setPopupDisplayEnabled (false, false, this);
    
    // Phase 6: Enable double-click reset to parameter default
    s.setDoubleClickReturnValue (true, defaultValue);
    
    // Phase 6: Disable mouse wheel
    s.setScrollWheelEnabled (false);
    
    // Phase 6: Enable Shift fine adjust using JUCE velocity mode (~2.5x finer, responsive at slow speeds)
    s.setVelocityModeParameters (0.4, 0, 0.0, true, juce::ModifierKeys::shiftModifier);
    
    s.setLookAndFeel (lookAndFeel.get());
}

void CompassEQAudioProcessorEditor::renderStaticLayer (juce::Graphics& g, float scaleKey, float physicalScale)
{
    // ===== Phase 5.0 — Asset-Ready Paint Layer (vector-only, no images) =====
    // Only paint changes. Layout is frozen. All drawing driven by assetSlots.

    // Phase 3C: PERFORMANCE BUDGET VERIFICATION CHECKLIST
    // ✓ No std::vector growth, no std::string concat, no Path churn that allocates
    // ✓ No Image reallocation in paint, no Gradient objects created per-frame without reuse/caching
    // ✓ Fonts from ladders/tokens (FontLadder returns const references, no per-frame construction)
    // ✓ All lambda captures are by reference or value (no heap allocations)
    // ✓ All coordinates are stack-allocated floats/ints
    // ✓ drawPlate, drawLine, drawText use stack-allocated parameters only
    // ✓ Caches rebuilt only on invalidation events (scaleKey change, resize) via AsyncUpdater

    // Phase 3 Fix: scaleKey and physicalScale are passed as parameters (from paint() or handleAsyncUpdate())
    // This ensures cache rebuild uses the same physicalScale that paint() observed

    // ===== PH9.4 — Paint hygiene ladder (no layout change) =====
    constexpr float kTitleA   = UIStyle::TextAlpha::title;
    constexpr float kHeaderA  = UIStyle::TextAlpha::header;
    constexpr float kMicroA   = UIStyle::TextAlpha::micro;
    constexpr float kTickA    = UIStyle::TextAlpha::tick;

    const auto editor = getLocalBounds();
    // Stage 3: value-only zone deltas (no seams) + Stage 2 uniform lighting lock
    drawFaceplateStage3ZonedNoSeams (
        g,
        editor,
        assetSlots.headerZone,
        assetSlots.filtersZone,
        assetSlots.bandsZone,
        assetSlots.trimZone,
        assetSlots.colLF,
        assetSlots.colLMF,
        assetSlots.colHMF,
        assetSlots.colHF,
        lfFreq.getBounds(),
        lmfFreq.getBounds(),
        hmfFreq.getBounds(),
        hfFreq.getBounds(),
        hpfFreq.getBounds(),
        lpfFreq.getBounds(),
        inputMeter.getBounds(),
        outputMeter.getBounds(),
        physicalScale);

    // ---- Global border ----
    // Stage 5.5 lock: outside colored lanes is neutral black; do not draw non-black plate chrome here.

    // Phase 1 depth model: rectangular recessed band wells (replace per-knob circular wells)
    drawBandWell (g, assetSlots.colLF,  physicalScale);
    drawBandWell (g, assetSlots.colLMF, physicalScale);
    drawBandWell (g, assetSlots.colHMF, physicalScale);
    drawBandWell (g, assetSlots.colHF,  physicalScale);

    // ---- Keep your existing Phase 3.3 text system (headers/legends/ticks) ----
    // Phase 2: Discrete font ladder by scaleKey
    const auto& titleFont  = UIStyle::FontLadder::titleFont (scaleKey);
    const auto& headerFont = UIStyle::FontLadder::headerFont (scaleKey);
    const auto& microFont  = UIStyle::FontLadder::microFont (scaleKey);
    const float hairlineStroke = UIStyle::StrokeLadder::hairlineStroke (scaleKey);

    // Engraved text helper (3-pass) — geometry locked (no layout drift)
    const float px = 1.0f / juce::jmax (1.0f, physicalScale); // 1 physical px in logical coords
    auto drawEngravedFitted = [&g, px] (const char* txt,
                                        int x, int y, int w, int h,
                                        juce::Justification just,
                                        int maxLines,
                                        float baseAlpha,
                                        juce::Colour mainCol)
    {
        // 1) Shadow (engraved depth)
        g.setColour (juce::Colours::black.withAlpha (juce::jlimit (0.0f, 1.0f, 0.75f * baseAlpha)));
        g.drawFittedText (txt,
                          juce::roundToInt ((float) x + 1.2f * px),
                          juce::roundToInt ((float) y + 1.2f * px),
                          w, h, just, maxLines);

        // 2) Main fill (slightly brighter)
        g.setColour (mainCol.withAlpha (juce::jlimit (0.0f, 1.0f, 1.00f * baseAlpha)));
        g.drawFittedText (txt, x, y, w, h, just, maxLines);

        // 3) Subtle bevel (thin bright pass)
        g.setColour (juce::Colours::white.withAlpha (juce::jlimit (0.0f, 1.0f, 0.15f * baseAlpha)));
        g.drawFittedText (txt,
                          x,
                          juce::roundToInt ((float) y - 0.5f * px),
                          w, h, just, maxLines);

        // 4) Top highlight (printed/engraved catch-light)
        g.setColour (juce::Colours::white.withAlpha (juce::jlimit (0.0f, 1.0f, 0.35f * baseAlpha)));
        g.drawFittedText (txt,
                          x,
                          juce::roundToInt ((float) y - 0.8f * px),
                          w, h, just, maxLines);
    };

    auto drawHeaderAbove = [&g, &headerFont, kHeaderA, physicalScale, drawEngravedFitted] (const char* txt, juce::Rectangle<int> b, int yOffset)
    {
        g.setFont (headerFont);
        // Phase 2: Snap baseline Y
        const float snappedY = UIStyle::Snap::snapPx ((float) (b.getY() + yOffset), physicalScale);
        drawEngravedFitted (txt, b.getX(), (int) snappedY, b.getWidth(), 12,
                            juce::Justification::centred, 1, kHeaderA, juce::Colours::white);
    };

    auto drawLegendBelow = [&g, &microFont, kHeaderA, physicalScale, drawEngravedFitted] (const char* txt, juce::Rectangle<int> b, int yOffset)
    {
        g.setFont (microFont);
        // Phase 2: Snap baseline Y
        const float snappedY = UIStyle::Snap::snapPx ((float) (b.getBottom() + yOffset), physicalScale);
        drawEngravedFitted (txt, b.getX(), (int) snappedY, b.getWidth(), 12,
                            juce::Justification::centred, 1, kHeaderA, juce::Colours::white);
    };

    auto drawTick = [&g, kTickA, physicalScale, hairlineStroke] (juce::Rectangle<int> b, int yOffset)
    {
        const float cx = UIStyle::Snap::snapPx ((float) b.getCentreX(), physicalScale);
        const float y0 = UIStyle::Snap::snapPx ((float) (b.getY() + yOffset), physicalScale);
        const float y1 = UIStyle::Snap::snapPx ((float) (b.getY() + yOffset + 6), physicalScale);
        g.setColour (UIStyle::Colors::foreground.withAlpha (kTickA));
        g.drawLine (cx, y0, cx, y1, hairlineStroke);
    };

    auto drawColLabel = [&g, &headerFont, kHeaderA, physicalScale, drawEngravedFitted] (const char* txt, juce::Rectangle<int> columnBounds, int y)
    {
        g.setFont (headerFont);
        // Phase 2: Snap baseline Y
        const float snappedY = UIStyle::Snap::snapPx ((float) y, physicalScale);
        drawEngravedFitted (txt, columnBounds.getX(), (int) snappedY, columnBounds.getWidth(), 14,
                            juce::Justification::centred, 1, kHeaderA, juce::Colours::white);
    };

    // Title (top-left; smaller; engraved) — geometry is fixed relative to editor
    {
        const int titleInset = 32;
        const int titleY = 28;
        auto titleRect = juce::Rectangle<int> (titleInset, titleY - 12, 200, 24);
        const float snappedY = UIStyle::Snap::snapPx ((float) titleRect.getY(), physicalScale);
        titleRect.setY ((int) snappedY);

        g.setFont (juce::Font (titleFont).withHeight (20.0f));
        drawEngravedFitted ("Compass EQ",
                            titleRect.getX(), titleRect.getY(), titleRect.getWidth(), titleRect.getHeight(),
                            juce::Justification::left, 1, kTitleA, juce::Colours::white);
    }

    // Column labels: sit right above the colored lanes (derived from the top of the KHz knobs + lane-top extra)
    constexpr int kBandLabelGap = 2;   // px gap between label box and lane top (slightly lower)
    constexpr int kBandLabelH   = 14;  // drawColLabel uses a 14px-high box
    constexpr int kLaneTopExtra = 18;  // must match drawFaceplateStage3ZonedNoSeams()
    const int yTopKHz = juce::jmin (lfFreq.getY(), lmfFreq.getY(),
                                   hmfFreq.getY(), hfFreq.getY());
    const int laneTopY = yTopKHz - kLaneTopExtra;
    const int bandLabelY = laneTopY - (kBandLabelH + kBandLabelGap);

    drawColLabel ("LF",  assetSlots.colLF,  bandLabelY);
    drawColLabel ("LMF", assetSlots.colLMF, bandLabelY);
    drawColLabel ("HMF", assetSlots.colHMF, bandLabelY);
    drawColLabel ("HF",  assetSlots.colHF,  bandLabelY);

    // Headers
    // Raise filter titles to avoid clashing with scale numbers
    drawHeaderAbove ("HPF", hpfFreq.getBounds(), -28);
    drawHeaderAbove ("LPF", lpfFreq.getBounds(), -28);

    drawHeaderAbove ("IN",  inputMeter.getBounds(),  -16);
    drawHeaderAbove ("OUT", outputMeter.getBounds(), -16);

    // Bottom trim labels: move IN/OUT below knobs (centered), and remove any "TRIM" wording.
    {
        int labelGap = 2; // primary clipping fix (was 6)
        const auto bypassB = globalBypass.getBounds();

        auto makeLabelRect = [&] (juce::Rectangle<int> knobB, int gap, const juce::Font& f)
        {
            // Step 2: small descent-aware lift to avoid bottom clipping
            int y = knobB.getBottom() + gap - (int) std::lround (f.getDescent() * 0.5f);

            // Step 3: clamp inside editor bounds (no UI resize)
            const int labelH = (int) std::ceil (f.getHeight());
            y = juce::jmin (y, editor.getBottom() - 2 - labelH);

            return juce::Rectangle<int> (knobB.getX(), y, knobB.getWidth(), labelH);
        };

        // Collision guard vs BYPASS: reduce gap, then reduce font size one step (trim labels only).
        bool reduceFont = false;
        auto f = headerFont;

        auto inLabel = makeLabelRect (inTrim.getBounds(), labelGap, f);
        auto outLabel = makeLabelRect (outTrim.getBounds(), labelGap, f);
        if (inLabel.intersects (bypassB) || outLabel.intersects (bypassB))
        {
            // Step 1 fallback: reduce gap further (toward 0) to avoid collision
            labelGap = 0;
            inLabel = makeLabelRect (inTrim.getBounds(), labelGap, f);
            outLabel = makeLabelRect (outTrim.getBounds(), labelGap, f);

            if (inLabel.intersects (bypassB) || outLabel.intersects (bypassB))
            {
                // Last resort: reduce font size one notch (trim labels only)
                reduceFont = true;
                f = headerFont.withHeight (headerFont.getHeight() - 1.0f);
                inLabel = makeLabelRect (inTrim.getBounds(), labelGap, f);
                outLabel = makeLabelRect (outTrim.getBounds(), labelGap, f);
            }
        }

        g.setFont (f);
        drawEngravedFitted ("IN",  inLabel.getX(),  inLabel.getY(),  inLabel.getWidth(),  inLabel.getHeight(),
                            juce::Justification::centred, 1, kHeaderA, juce::Colours::white);
        drawEngravedFitted ("OUT", outLabel.getX(), outLabel.getY(), outLabel.getWidth(), outLabel.getHeight(),
                            juce::Justification::centred, 1, kHeaderA, juce::Colours::white);
    }

    // Legends
    drawLegendBelow ("KHz", lfFreq.getBounds(),  2);
    drawLegendBelow ("GR",  lfGain.getBounds(),  2);

    drawLegendBelow ("KHz", lmfFreq.getBounds(), 2);
    drawLegendBelow ("GR",  lmfGain.getBounds(), 2);
    drawLegendBelow ("Q",    lmfQ.getBounds(),    2);

    drawLegendBelow ("KHz", hmfFreq.getBounds(), 2);
    drawLegendBelow ("GR",  hmfGain.getBounds(), 2);
    drawLegendBelow ("Q",    hmfQ.getBounds(),    2);

    drawLegendBelow ("KHz", hfFreq.getBounds(),  2);
    drawLegendBelow ("GR",  hfGain.getBounds(),  2);

    // (No "FREQ" legend under HPF/LPF per spec.)

    // (No "TRIM" legend under the bottom trim knobs per spec.)

    // ===== Scale markings (SSL-style) — semi-circle around knob: ticks + key numbers =====
    // Vector-only, no assets, no layout changes. Uses engraved text helper for numbers.
    {
        constexpr int numTicks = 13;
        const float startRad = juce::MathConstants<float>::pi * 1.5f - juce::MathConstants<float>::pi * 0.833f;
        const float endRad   = juce::MathConstants<float>::pi * 1.5f + juce::MathConstants<float>::pi * 0.833f;
        const float range    = endRad - startRad;

        auto drawScaleMarkings = [&g, physicalScale, drawEngravedFitted, kMicroA, startRad, range] (juce::Rectangle<int> knobBounds,
                                                                                                     const char* const* numbers,
                                                                                                     int numberCount)
        {
            if (knobBounds.isEmpty())
                return;

            const auto b = knobBounds.toFloat();
            const float cx = UIStyle::Snap::snapPx (b.getCentreX(), physicalScale);
            const float cy = UIStyle::Snap::snapPx (b.getCentreY() + b.getHeight() * 0.05f, physicalScale); // tighter / higher
            const float radius = b.getWidth() * 0.50f; // smaller radius to hug knob

            g.setColour (juce::Colours::silver.withAlpha (0.40f));

            for (int i = 0; i <= numTicks; ++i)
            {
                const float t = (float) i / (float) numTicks;
                const float ang = startRad + t * range;
                const float len = (i % 3 == 0) ? 12.0f : 7.0f;

                juce::Point<float> inner (cx + (radius - len) * std::cos (ang),
                                          cy + (radius - len) * std::sin (ang));
                juce::Point<float> outer (cx + radius * std::cos (ang),
                                          cy + radius * std::sin (ang));

                inner = UIStyle::Snap::snapPoint (inner, physicalScale);
                outer = UIStyle::Snap::snapPoint (outer, physicalScale);

                g.drawLine (inner.x, inner.y, outer.x, outer.y, 1.2f);
            }

            if (numbers == nullptr || numberCount < 2)
                return;

            for (int i = 0; i < numberCount; ++i)
            {
                const float t = (float) i / (float) (numberCount - 1);
                const float ang = startRad + t * range;

                const float x = cx + (radius + 8.0f) * std::cos (ang);
                const float y = cy + (radius + 8.0f) * std::sin (ang);

                const int w = 24;
                const int h = 14;
                const int xi = (int) std::lround (UIStyle::Snap::snapPx (x - (float) (w / 2), physicalScale));
                const int yi = (int) std::lround (UIStyle::Snap::snapPx (y - (float) (h / 2), physicalScale));

                drawEngravedFitted (numbers[i], xi, yi, w, h,
                                    juce::Justification::centred, 1,
                                    kMicroA, juce::Colours::white.withAlpha (0.85f));
            }
        };

        // Number sets (fixed, no allocations)
        static const char* const kFreqLF[]   = { "20", "200", "400", "600", "800" };      // 20–800 Hz
        static const char* const kFreqLMF[]  = { "120", "1k", "2k", "3k", "4k" };         // 120–4k Hz
        static const char* const kFreqHMF[]  = { "600", "4k", "8k", "11k", "15k" };       // 600–15k Hz
        static const char* const kFreqHF[]   = { "1.5k", "6.5k", "12k", "17k", "22k" };   // 1.5–22k Hz
        static const char* const kGain[]     = { "-18", "-12", "-6", "0", "+6", "+12", "+18" };
        static const char* const kQ[]        = { "0.5", "1", "2", "4", "8" };

        // LF
        drawScaleMarkings (lfFreq.getBounds(), kFreqLF,  (int) (sizeof (kFreqLF) / sizeof (kFreqLF[0])));
        drawScaleMarkings (lfGain.getBounds(), kGain,    (int) (sizeof (kGain) / sizeof (kGain[0])));

        // LMF
        drawScaleMarkings (lmfFreq.getBounds(), kFreqLMF, (int) (sizeof (kFreqLMF) / sizeof (kFreqLMF[0])));
        drawScaleMarkings (lmfGain.getBounds(), kGain,    (int) (sizeof (kGain) / sizeof (kGain[0])));
        drawScaleMarkings (lmfQ.getBounds(),    kQ,       (int) (sizeof (kQ) / sizeof (kQ[0])));

        // HMF
        drawScaleMarkings (hmfFreq.getBounds(), kFreqHMF, (int) (sizeof (kFreqHMF) / sizeof (kFreqHMF[0])));
        drawScaleMarkings (hmfGain.getBounds(), kGain,    (int) (sizeof (kGain) / sizeof (kGain[0])));
        drawScaleMarkings (hmfQ.getBounds(),    kQ,       (int) (sizeof (kQ) / sizeof (kQ[0])));

        // HF
        drawScaleMarkings (hfFreq.getBounds(), kFreqHF,  (int) (sizeof (kFreqHF) / sizeof (kFreqHF[0])));
        drawScaleMarkings (hfGain.getBounds(), kGain,    (int) (sizeof (kGain) / sizeof (kGain[0])));

        // Filters (frequency)
        static const char* const kFreqHPF[]  = { "20", "300", "500", "750", "1k" };
        static const char* const kFreqLPF[]  = { "3k", "7k", "11k", "15k", "20k" };
        drawScaleMarkings (hpfFreq.getBounds(), kFreqHPF, (int) (sizeof (kFreqHPF) / sizeof (kFreqHPF[0])));
        drawScaleMarkings (lpfFreq.getBounds(), kFreqLPF, (int) (sizeof (kFreqLPF) / sizeof (kFreqLPF[0])));
    }

    // Ticks
    drawTick (lfFreq.getBounds(),  -2);  drawTick (lfGain.getBounds(), -2);
    drawTick (lmfFreq.getBounds(), -2);  drawTick (lmfGain.getBounds(), -2); drawTick (lmfQ.getBounds(), -2);
    drawTick (hmfFreq.getBounds(), -2);  drawTick (hmfGain.getBounds(), -2); drawTick (hmfQ.getBounds(), -2);
    drawTick (hfFreq.getBounds(),  -2);  drawTick (hfGain.getBounds(), -2);

    drawTick (hpfFreq.getBounds(), -2);
    drawTick (lpfFreq.getBounds(), -2);

    drawTick (inTrim.getBounds(),  -2);
    drawTick (outTrim.getBounds(), -2);

    // Keep your Phase 4 debug overlay (still OFF by default)
    if constexpr (kAssetSlotDebug == 1)
    {
        auto draw = [&g] (juce::Rectangle<int> r)
        {
            g.setColour (UIStyle::Colors::foreground.withAlpha (UIStyle::UIAlpha::debugOverlay));
            g.drawRect (r, 1);
        };

        draw (assetSlots.headerZone);
        draw (assetSlots.filtersZone);
        draw (assetSlots.bandsZone);
        draw (assetSlots.trimZone);

        draw (assetSlots.colLF);
        draw (assetSlots.colLMF);
        draw (assetSlots.colHMF);
        draw (assetSlots.colHF);
    }

    // ===== Phase 6.0 paint-audit overlay (OFF by default) =====
    if constexpr (kPaintAuditOverlay == 1)
    {
        auto box = [&g] (juce::Rectangle<int> r, float a)
        {
            if (r.isEmpty()) return;
            g.setColour (UIStyle::Colors::foreground.withAlpha (a));
            g.drawRect (r, 1);
        };

        // Asset slots
        box (assetSlots.headerZone, UIStyle::UIAlpha::auditOverlay);
        box (assetSlots.filtersZone, UIStyle::UIAlpha::auditOverlay);
        box (assetSlots.bandsZone, UIStyle::UIAlpha::auditOverlay);
        box (assetSlots.trimZone, UIStyle::UIAlpha::auditOverlay);

        box (assetSlots.colLF, UIStyle::UIAlpha::auditOverlay);
        box (assetSlots.colLMF, UIStyle::UIAlpha::auditOverlay);
        box (assetSlots.colHMF, UIStyle::UIAlpha::auditOverlay);
        box (assetSlots.colHF, UIStyle::UIAlpha::auditOverlay);

        // Knob bounds (exact control bounds)
        box (lfFreq.getBounds(), UIStyle::UIAlpha::auditOverlayKnob); box (lfGain.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);
        box (lmfFreq.getBounds(), UIStyle::UIAlpha::auditOverlayKnob); box (lmfGain.getBounds(), UIStyle::UIAlpha::auditOverlayKnob); box (lmfQ.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);
        box (hmfFreq.getBounds(), UIStyle::UIAlpha::auditOverlayKnob); box (hmfGain.getBounds(), UIStyle::UIAlpha::auditOverlayKnob); box (hmfQ.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);
        box (hfFreq.getBounds(), UIStyle::UIAlpha::auditOverlayKnob); box (hfGain.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);

        box (hpfFreq.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);
        box (lpfFreq.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);

        box (inTrim.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);
        box (outTrim.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);

        // Meters
        box (inputMeter.getBounds(), UIStyle::UIAlpha::auditOverlayMeter);
        box (outputMeter.getBounds(), UIStyle::UIAlpha::auditOverlayMeter);
    }
}

void CompassEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Phase 3C: PERFORMANCE BUDGET VERIFICATION CHECKLIST
    // ✓ No std::vector growth, no std::string concat, no Path churn that allocates
    // ✓ No Image reallocation in paint (cache image is pre-allocated, only drawn via drawImageTransformed)
    // ✓ No Gradient objects created per-frame without reuse/caching
    // ✓ Fonts from ladders/tokens (not applicable here, handled in renderStaticLayer)
    // ✓ All coordinates are stack-allocated floats/ints
    // ✓ Caches rebuilt only on invalidation events (scaleKey change, resize) via AsyncUpdater
    // ✓ Cache rebuild happens in handleAsyncUpdate() on UI thread, NOT in paint()
    
    // ===== Phase 1: Scale Source of Truth + scaleKey policy =====
    // Derive physical pixel scale from active editor paint graphics context
    const auto physicalScale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    physicalScaleLastPaint = physicalScale;
    
    // Compute rawKey = round(physicalScale * 100) / 100
    const float rawKey = std::round (physicalScale * 100.0f) / 100.0f;
    
    // macOS snap-to-known-values (tolerance 0.02)
    float scaleKey;
    if (std::abs (rawKey - 2.00f) <= 0.02f)
        scaleKey = 2.00f;
    else if (std::abs (rawKey - 1.00f) <= 0.02f)
        scaleKey = 1.00f;
    else
        scaleKey = rawKey;
    
    // Stability window: add to history
    scaleKeyHistory[scaleKeyHistoryIndex] = scaleKey;
    scaleKeyHistoryIndex = (scaleKeyHistoryIndex + 1) % stabilityWindowSize;
    if (scaleKeyHistoryCount < stabilityWindowSize)
        scaleKeyHistoryCount++;
    
    // Check if all last N values match (stability requirement)
    // Note: scaleKeyHistoryIndex points to where NEXT value will go
    bool isStable = (scaleKeyHistoryCount >= stabilityWindowSize);
    if (isStable)
    {
        // Get the most recent value (the one we just added, which is at index-1)
        const int mostRecentIdx = (scaleKeyHistoryIndex - 1 + stabilityWindowSize) % stabilityWindowSize;
        const float mostRecent = scaleKeyHistory[mostRecentIdx];
        
        // Check that the last N values (in chronological order) all match
        for (int i = 0; i < stabilityWindowSize; ++i)
        {
            const int idx = (mostRecentIdx - i + stabilityWindowSize) % stabilityWindowSize;
            if (std::abs (scaleKeyHistory[idx] - mostRecent) > 0.001f)
            {
                isStable = false;
                break;
            }
        }
    }
    
    // Rate limiting: check if enough time has passed since last change
    const auto currentTime = juce::Time::currentTimeMillis();
    const bool rateLimitOk = (currentTime - lastScaleKeyChangeTime) >= rateLimitMs;
    
    // Update active scaleKey if stable and rate limit allows
    if (isStable && rateLimitOk)
    {
        // All values are the same if stable, use the most recent
        const int mostRecentIdx = (scaleKeyHistoryIndex - 1 + stabilityWindowSize) % stabilityWindowSize;
        const float candidateKey = scaleKeyHistory[mostRecentIdx];
        if (std::abs (candidateKey - scaleKeyActive) > 0.001f)
        {
            scaleKeyActive = candidateKey;
            lastScaleKeyChangeTime = currentTime;
            
            // Phase 3A: Invalidate cache when scaleKeyActive changes
            staticCacheDirty.store (true, std::memory_order_release);
            if (! staticCacheRebuildPending.exchange (true, std::memory_order_acq_rel))
                triggerAsyncUpdate();
        }
    }
    // During transition: continue using last-valid active scaleKey (already set)
    
    // ===== Phase 2: Static Layer Cache =====
    const float sk = getScaleKeyActive();
    const float physical = juce::jmax (1.0f, physicalScale);
    const int w = getWidth();
    const int h = getHeight();
    const int pw = juce::roundToInt ((double) w * (double) physical);
    const int ph = juce::roundToInt ((double) h * (double) physical);
    
    // Phase 3A: Check if cache is valid and matches current scaleKey and physical pixel size
    const bool cacheValid = staticCache.valid() 
                         && std::abs (staticCache.scaleKey - sk) < 0.001f
                         && staticCache.pixelW == pw
                         && staticCache.pixelH == ph;
    
    if (cacheValid)
    {
        // Draw cached image with transform back to logical coords
        g.drawImageTransformed (staticCache.image, juce::AffineTransform::scale (1.0f / physical));
    }
    else
    {
        // Fallback: draw uncached using the REAL physicalScale from live editor context
        renderStaticLayer (g, sk, physical);
        
        // Mark dirty and trigger async rebuild (outside paint) - prevent spam
        staticCacheDirty.store (true, std::memory_order_release);

        if (! staticCacheRebuildPending.exchange (true, std::memory_order_acq_rel))
            triggerAsyncUpdate();
    }

}

void CompassEQAudioProcessorEditor::handleAsyncUpdate()
{
    staticCacheRebuildPending.store (false, std::memory_order_release);
    
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    
    // Phase 4: Early return if teardown is in progress
    if (isTearingDown)
        return;
    
    // Rebuild cache only if visible and bounds are valid
    if (! isVisible() || getBounds().isEmpty())
        return;
    
    const int w = getWidth();
    const int h = getHeight();
    if (w <= 0 || h <= 0)
        return;
    
    // Phase 3 Fix: Use the SAME physicalScale that paint() observed (from physicalScaleLastPaint)
    const float physicalScale = juce::jmax (1.0f, getPhysicalScaleLastPaint());
    const int pw = juce::roundToInt ((double) w * (double) physicalScale);
    const int ph = juce::roundToInt ((double) h * (double) physicalScale);
    
    if (pw <= 0 || ph <= 0)
        return;
    
    const float sk = getScaleKeyActive();
    
    // Phase 3A: Rebuild cache ONLY if dirty OR cache.scaleKey != sk OR pixel size mismatch
    if (! staticCacheDirty.load (std::memory_order_acquire)
        && std::abs (staticCache.scaleKey - sk) < 0.001f
        && staticCache.valid()
        && staticCache.pixelW == pw
        && staticCache.pixelH == ph)
        return;
    
    // Phase 3A: Create image at physical pixel size (rebuild happens on UI thread, NOT in paint)
    juce::Image img (juce::Image::ARGB, pw, ph, true);
    juce::Graphics cg (img);
    cg.addTransform (juce::AffineTransform::scale (physicalScale));
    // Phase 3 Fix: Pass physicalScale to renderStaticLayer so snapping uses the same scale paint() observed
    renderStaticLayer (cg, sk, physicalScale);
    
    // Phase 3A: Update cache with scaleKey and physical pixel dimensions
    staticCache.image = std::move (img);
    staticCache.scaleKey = sk;
    staticCache.pixelW = pw;
    staticCache.pixelH = ph;
    staticCacheDirty.store (false, std::memory_order_release);
    
    // Trigger repaint to use new cache
    repaint();
}

void CompassEQAudioProcessorEditor::resized()
{
    // ===== Layout Freeze Spec v0.1 (AUTHORITATIVE) =====
    // Editor: 760 x 460
    // Grid base: 8 (all integer placement)
    // Zones: 64 / 72 / 240 / 84 (sum 460)
    // Margins: L/R = 24
    // Columns: LF 160, LMF 168, HMF 168, HF 160
    // Gaps: 19 / 19 / 18 (deterministic)
    // Knobs: Primary 56, Secondary 48, Tertiary 40
    // Meters: 18w, header padding 8 top/bottom

    const int editorW = kEditorW;
    const int editorH = kEditorH;
    (void) editorH;

    const int marginL = 24;
    const int marginR = 24;
    const int usableW = editorW - marginL - marginR; // 712

    // Zone Y positions
    const int z1Y = 0;
    const int z1H = 64;

    const int z2Y = z1Y + z1H;
    const int z2H = 72;

    const int z3Y = z2Y + z2H;
    const int z3H = 240;

    const int z4Y = z3Y + z3H;
    const int z4H = 84;

    // ----- Zone 1: Header (meters) -----
    // ===== PH9.3 — Bottom-anchored meters (bottom -> mid) =====
    {
        constexpr int meterW = 18;

        const int inMeterX  = 24;
        const int outMeterX = getWidth() - 24 - meterW;

        // Bottom anchor: sit above the bottom border, but below trim zone content
        const int meterBottomPad = 12;
        const int meterBottomY   = getHeight() - meterBottomPad;

        // Top target: around the middle of the UI (use the filters/bands boundary as a stable "mid")
        const int midY = z3Y; // filters/bands boundary (stable reference)

        // Small top pad so it doesn't kiss the mid line
        const int meterTopPad = 10;
        const int meterY = midY + meterTopPad;

        const int meterH = juce::jmax (60, meterBottomY - meterY);

        inputMeter.setBounds  (inMeterX,  meterY, meterW, meterH);
        outputMeter.setBounds (outMeterX, meterY, meterW, meterH);
    }

    // ----- Zone 2: Filters (HPF/LPF) -----
    const int filterKnob = 48;
    const int filterSpacing = 80;
    const int filtersTotalW = filterKnob + filterSpacing + filterKnob; // 128

    const int filtersStartX = marginL + ((usableW - filtersTotalW) / 2); // 316
    // Stage 5.4b (LOCKED): place HPF/LPF knob centers using topZoneRect:
    // HPF_LPF_CENTER_Y = topZoneRect.y + (topZoneRect.height * 0.40)
    // Filter bar Y geometry lock:
    //   - labelTopPad = 28 (header yOffset=-16, height=12)
    //   - padTop = 10, padBottom = 10
    //   - barHeight = filterKnob + labelTopPad + padTop + padBottom = 96
    // Raise HPF/LPF higher (negative offset = higher on screen)
    const int filtersY = z2Y - 20;

    hpfFreq.setBounds (filtersStartX,                        filtersY, filterKnob, filterKnob);
    lpfFreq.setBounds (filtersStartX + filterKnob + filterSpacing, filtersY, filterKnob, filterKnob);

    // ----- Zone 3: EQ Bands -----
    // Columns + deterministic gaps: 19 / 19 / 18
    const int gap1 = 19;
    const int gap2 = 19;
    const int gap3 = 18;

    const int lfW  = 160;
    const int lmfW = 168;
    const int hmfW = 168;
    const int hfW  = 160;

    const int lfX  = marginL;
    const int lmfX = lfX  + lfW  + gap1; // 203
    const int hmfX = lmfX + lmfW + gap2; // 390
    const int hfX  = hmfX + hmfW + gap3; // 576

    // Knob sizes
    const int kPrimary   = 56;
    const int kSecondary = 48;
    const int kTertiary  = 40;

    // Zone 3 vertical centering math (integers)
    // Increase vertical spacing in stacks
    const int stackSpacing = 24;

    // LMF/HMF stack
    // Middle lanes: move all three knobs up together (preserve spacing).
    // Target: top of the KHz knob kisses the top of the colored lane.
    constexpr int midLaneShiftUp = 8;
    const int stack3Top = (z3Y + 14) - midLaneShiftUp;
    const int lmfFreqY  = stack3Top;
    const int lmfQY     = (z3Y + z3H - kTertiary - 10) - midLaneShiftUp;
    // Place GAIN so gaps are even: (Freq -> Gain) == (Gain -> Q)
    const int lmfGap    = juce::jmax (0, (lmfQY - lmfFreqY - kSecondary - kPrimary) / 2);
    const int lmfGainY  = lmfFreqY + kSecondary + lmfGap;

    // LF/HF stack
    const int stack2Top = z3Y + 50; // More centered
    const int lfFreqY   = stack2Top;
    const int lfGainY   = lfFreqY + 48 + stackSpacing + 10;

    auto centerX = [] (int colX, int colW, int knobW) -> int
    {
        return colX + ((colW - knobW) / 2);
    };

    // LF (Freq 48, Gain 56)
    lfFreq.setBounds (centerX (lfX, lfW, kSecondary), lfFreqY, kSecondary, kSecondary);
    lfGain.setBounds (centerX (lfX, lfW, kPrimary),   lfGainY, kPrimary,   kPrimary);

    // LMF (Freq 48, Gain 56, Q 40)
    lmfFreq.setBounds (centerX (lmfX, lmfW, kSecondary), lmfFreqY, kSecondary, kSecondary);
    lmfGain.setBounds (centerX (lmfX, lmfW, kPrimary),   lmfGainY, kPrimary,   kPrimary);
    lmfQ.setBounds    (centerX (lmfX, lmfW, kTertiary),  lmfQY,    kTertiary,  kTertiary);

    // HMF (Freq 48, Gain 56, Q 40)
    hmfFreq.setBounds (centerX (hmfX, hmfW, kSecondary), lmfFreqY, kSecondary, kSecondary);
    hmfGain.setBounds (centerX (hmfX, hmfW, kPrimary),   lmfGainY, kPrimary,   kPrimary);
    hmfQ.setBounds    (centerX (hmfX, hmfW, kTertiary),  lmfQY,    kTertiary,  kTertiary);

    // HF (Freq 48, Gain 56)
    hfFreq.setBounds (centerX (hfX, hfW, kSecondary), lfFreqY, kSecondary, kSecondary);
    hfGain.setBounds (centerX (hfX, hfW, kPrimary),   lfGainY, kPrimary,   kPrimary);

    // STAGE 5.8 — EXPAND BAND SPAN TO METERS (LOCKED)
    // Re-run the equal-width lane solver using meter-derived span, then recenter all band stacks.
    {
        constexpr int g = 8; // must match assetSlots expansion grid
        const auto editor = getLocalBounds();

        const auto colLFRect  = lfFreq.getBounds().getUnion (lfGain.getBounds());
        const auto colLMFRect = lmfFreq.getBounds().getUnion (lmfGain.getBounds()).getUnion (lmfQ.getBounds());
        const auto colHMFRect = hmfFreq.getBounds().getUnion (hmfGain.getBounds()).getUnion (hmfQ.getBounds());
        const auto colHFRect  = hfFreq.getBounds().getUnion (hfGain.getBounds());

        const auto bandsUnion =
            colLFRect.getUnion (colLMFRect).getUnion (colHMFRect).getUnion (colHFRect);
        const auto bandsZoneRect = bandsUnion.expanded (g * 2, g * 2).getIntersection (editor);

        constexpr float meterGap = 10.0f; // LOCKED
        const float bandsSpanLeft  = (float) inputMeter.getBounds().getRight() + meterGap;
        const float bandsSpanRight = (float) outputMeter.getBounds().getX() - meterGap;
        const float bandsSpanW = bandsSpanRight - bandsSpanLeft;

        constexpr float dividerW = 1.0f;
        const float laneW = (bandsSpanW - 3.0f * dividerW) / 4.0f;

        const float x0 = bandsSpanLeft;
        const float xDiv1 = x0 + laneW;
        const float xDiv2 = x0 + 2.0f * laneW + 1.0f * dividerW;
        const float xDiv3 = x0 + 3.0f * laneW + 2.0f * dividerW;

        const float laneLFLeft  = x0;
        const float laneLFRight = xDiv1;
        const float laneLMFLeft  = xDiv1 + dividerW;
        const float laneLMFRight = xDiv2;
        const float laneHMFLeft  = xDiv2 + dividerW;
        const float laneHMFRight = xDiv3;
        const float laneHFLeft   = xDiv3 + dividerW;
        const float laneHFRight  = x0 + 4.0f * laneW + 3.0f * dividerW;

        auto clampDxToLane = [&] (juce::Rectangle<int> stack, float laneLeft, float laneRight, int dx) -> int
        {
            const int minDx = (int) std::ceil (laneLeft) - stack.getX();
            const int maxDx = (int) std::floor (laneRight - 1.0f) - stack.getRight();
            return juce::jlimit (minDx, maxDx, dx);
        };

        auto centerStackInLane = [&] (juce::Rectangle<int> laneStack, float laneLeft, float laneRight, auto&& translateFn)
        {
            const float laneCx = 0.5f * (laneLeft + laneRight);
            int dx = (int) std::lround (laneCx - (float) laneStack.getCentreX());
            dx = clampDxToLane (laneStack, laneLeft, laneRight, dx);
            translateFn (dx);
        };

        // LF stack (2 knobs)
        centerStackInLane (lfFreq.getBounds().getUnion (lfGain.getBounds()),
                           laneLFLeft, laneLFRight,
                           [&] (int dx)
                           {
                               lfFreq.setBounds (lfFreq.getBounds().translated (dx, 0));
                               lfGain.setBounds (lfGain.getBounds().translated (dx, 0));
                           });

        // LMF stack (3 knobs)
        centerStackInLane (lmfFreq.getBounds().getUnion (lmfGain.getBounds()).getUnion (lmfQ.getBounds()),
                           laneLMFLeft, laneLMFRight,
                           [&] (int dx)
                           {
                               lmfFreq.setBounds (lmfFreq.getBounds().translated (dx, 0));
                               lmfGain.setBounds (lmfGain.getBounds().translated (dx, 0));
                               lmfQ.setBounds    (lmfQ.getBounds().translated (dx, 0));
                           });

        // HMF stack (3 knobs)
        centerStackInLane (hmfFreq.getBounds().getUnion (hmfGain.getBounds()).getUnion (hmfQ.getBounds()),
                           laneHMFLeft, laneHMFRight,
                           [&] (int dx)
                           {
                               hmfFreq.setBounds (hmfFreq.getBounds().translated (dx, 0));
                               hmfGain.setBounds (hmfGain.getBounds().translated (dx, 0));
                               hmfQ.setBounds    (hmfQ.getBounds().translated (dx, 0));
                           });

        // HF stack (2 knobs)
        centerStackInLane (hfFreq.getBounds().getUnion (hfGain.getBounds()),
                           laneHFLeft, laneHFRight,
                           [&] (int dx)
                           {
                               hfFreq.setBounds (hfFreq.getBounds().translated (dx, 0));
                               hfGain.setBounds (hfGain.getBounds().translated (dx, 0));
                           });
    }

    // ----- Zone 4: Trim + Bypass -----
    // ===== PH9.4 — Zone 4: Center BYPASS + symmetric trims =====
    {
        constexpr int g = 8;

        // Re-derive Zone 4 from editor bounds (no floats, deterministic)
        auto editor = getLocalBounds();
        auto zone4  = editor.removeFromBottom (84).reduced (24, 0); // Zone 4 height per freeze spec

        // Vertical centering in Zone 4
        constexpr int trimSize   = 52; // slightly smaller IN/OUT knobs
        constexpr int bypassW    = 160;
        constexpr int bypassH    = 26; // STAGE: SSL BYPASS latch (LOCKED)

        // Keep BYPASS where it was, but drop trims slightly to avoid lane overlap.
        const int bypassCy = zone4.getCentreY() - 10;
        const int trimCy   = bypassCy + 10;

        // BYPASS centered
        const auto bypassBounds = juce::Rectangle<int> (0, 0, bypassW, bypassH)
                                    .withCentre ({ zone4.getCentreX(), bypassCy });
        globalBypass.setBounds (bypassBounds);

        // Trims: symmetric around bypass, keep >= 32px spacing (4g)
        constexpr int minGapToBypass = 32;

        const int leftTrimCx  = bypassBounds.getX() - minGapToBypass - (trimSize / 2);
        const int rightTrimCx = bypassBounds.getRight() + minGapToBypass + (trimSize / 2);

        inTrim.setBounds  (juce::Rectangle<int> (0, 0, trimSize, trimSize).withCentre ({ leftTrimCx,  trimCy }));
        outTrim.setBounds (juce::Rectangle<int> (0, 0, trimSize, trimSize).withCentre ({ rightTrimCx, trimCy }));
    }

    // ===== Phase 4.0 — Asset Slot Map (derived from existing bounds) =====
    {
        constexpr int g = 8;

        assetSlots = {}; // reset

        assetSlots.editor = getLocalBounds();

        // Exact component bounds
        assetSlots.inputMeter  = inputMeter.getBounds();
        assetSlots.outputMeter = outputMeter.getBounds();

        assetSlots.hpfKnob = hpfFreq.getBounds();
        assetSlots.lpfKnob = lpfFreq.getBounds();

        assetSlots.lfFreq = lfFreq.getBounds();   assetSlots.lfGain = lfGain.getBounds();

        assetSlots.lmfFreq = lmfFreq.getBounds(); assetSlots.lmfGain = lmfGain.getBounds(); assetSlots.lmfQ = lmfQ.getBounds();
        assetSlots.hmfFreq = hmfFreq.getBounds(); assetSlots.hmfGain = hmfGain.getBounds(); assetSlots.hmfQ = hmfQ.getBounds();

        assetSlots.hfFreq = hfFreq.getBounds();   assetSlots.hfGain = hfGain.getBounds();

        assetSlots.inTrim  = inTrim.getBounds();
        assetSlots.outTrim = outTrim.getBounds();
        assetSlots.bypass  = globalBypass.getBounds();

        // Unions (derived only)
        assetSlots.filtersUnion = assetSlots.hpfKnob.getUnion (assetSlots.lpfKnob);

        assetSlots.bandsUnion =
            assetSlots.lfFreq.getUnion (assetSlots.lfGain)
                .getUnion (assetSlots.lmfFreq).getUnion (assetSlots.lmfGain).getUnion (assetSlots.lmfQ)
                .getUnion (assetSlots.hmfFreq).getUnion (assetSlots.hmfGain).getUnion (assetSlots.hmfQ)
                .getUnion (assetSlots.hfFreq).getUnion (assetSlots.hfGain);

        assetSlots.trimsUnion = assetSlots.inTrim.getUnion (assetSlots.outTrim).getUnion (assetSlots.bypass);

        // Column unions (useful for later panel assets)
        assetSlots.colLF  = assetSlots.lfFreq.getUnion (assetSlots.lfGain);
        assetSlots.colLMF = assetSlots.lmfFreq.getUnion (assetSlots.lmfGain).getUnion (assetSlots.lmfQ);
        assetSlots.colHMF = assetSlots.hmfFreq.getUnion (assetSlots.hmfGain).getUnion (assetSlots.hmfQ);
        assetSlots.colHF  = assetSlots.hfFreq.getUnion (assetSlots.hfGain);

        // Major zones (derived from component bounds, expanded by grid)
        assetSlots.headerZone  = assetSlots.inputMeter.getUnion (assetSlots.outputMeter).expanded (g, g);
        assetSlots.filtersZone = assetSlots.filtersUnion.expanded (g * 2, g * 2);
        assetSlots.bandsZone   = assetSlots.bandsUnion.expanded (g * 2, g * 2);
        assetSlots.trimZone    = assetSlots.trimsUnion.expanded (g * 2, g * 2);

        // Clamp to editor
        auto clamp = [&] (juce::Rectangle<int>& r)
        {
            r = r.getIntersection (assetSlots.editor);
        };

        clamp (assetSlots.headerZone);
        clamp (assetSlots.filtersZone);
        clamp (assetSlots.bandsZone);
        clamp (assetSlots.trimZone);
    }
    
    // Phase 6: Position fixed value readout (fixed bounds, never changes)
    valueReadout.setBounds (kReadoutX, kReadoutY, kReadoutW, kReadoutH);
    
    // ===== Phase 2: Invalidate cache on resize =====
    staticCacheDirty.store (true, std::memory_order_release);

    if (! staticCacheRebuildPending.exchange (true, std::memory_order_acq_rel))
        triggerAsyncUpdate();
}
