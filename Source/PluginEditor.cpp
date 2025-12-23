#include "PluginEditor.h"
#include "Phase1Spec.h"
#include "UIStyle.h"

static constexpr int kEditorW = 760;
static constexpr int kEditorH = 420;

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
        constexpr float satRatio = 0.55f;
        constexpr float maxChromaC = 0.30f;
        constexpr float luminanceDeltaLstar = -10.0f;

        // Luminance source remains the knob body constant (neutral), per locked rules.
        const float kR = srgbToLinear1 (knobBodySrgbNeutral.getFloatRed());
        const float kG = srgbToLinear1 (knobBodySrgbNeutral.getFloatGreen());
        const float kB = srgbToLinear1 (knobBodySrgbNeutral.getFloatBlue());
        OKLab knobLab = linearSrgbToOklab (kR, kG, kB);

        const float L100 = knobLab.L * 100.0f;
        const float newL100 = juce::jlimit (0.0f, 100.0f, L100 + luminanceDeltaLstar);

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
        juce::Rectangle<int> hpfKnob,
        juce::Rectangle<int> lpfKnob,
        juce::Rectangle<int> meterInRect,
        juce::Rectangle<int> meterOutRect,
        float physicalScale)
    {
        if (editor.isEmpty())
            return;

        // STAGE 5.5 — NEUTRAL BLACK BASE PLATE (LOCKED)
        // Any region outside the colored band lanes must render as neutral black.
        // One base plate black constant only.
        const auto plateBaseBlack = juce::Colour::fromRGB (18, 18, 18);
        g.setColour (plateBaseBlack);
        g.fillRect (editor);

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

                // Lane fills (flat, same opacity, per-band hue constants)
                constexpr float laneOpacity = 0.14f;
                auto fillLane = [&] (float xLeft, float w, float hueDeg)
                {
                    const auto rf = juce::Rectangle<float> (xLeft, (float) bandsRect.getY(), w, (float) bandsRect.getHeight());
                    if (rf.isEmpty())
                        return;
                    const auto c = stage5_bandHueToSectionBg_OkLabLinear (hueDeg, UIStyle::Colors::knobBody).withAlpha (laneOpacity);
                    g.setColour (c);
                    g.fillRect (rf);
                };

                // Construct lanes from span (do not reuse old midpoints)
                fillLane (x0,                         laneW, UIStyle::Colors::bandHueLF);
                fillLane (x0 + laneW + dividerW,      laneW, UIStyle::Colors::bandHueLMF);
                fillLane (x0 + 2.0f * laneW + 2*dividerW, laneW, UIStyle::Colors::bandHueHMF);
                fillLane (x0 + 3.0f * laneW + 3*dividerW, laneW, UIStyle::Colors::bandHueHF);

                // Dividers between adjacent bands only (LF|LMF|HMF|HF)
                const auto dividerCol = juce::Colour::fromRGB (230, 230, 230).withAlpha (0.65f);
                g.setColour (dividerCol);
                const float y1 = (float) bandsRect.getY();
                const float y2 = (float) bandsRect.getBottom();
                g.drawLine (xDiv1, y1, xDiv1, y2, 1.0f);
                g.drawLine (xDiv2, y1, xDiv2, y2, 1.0f);
                g.drawLine (xDiv3, y1, xDiv3, y2, 1.0f);

                // Outer divider frame around the entire colored EQ band lane block (LF/LMF/HMF/HF)
                // Style is locked to match internal dividers (same 1px, same colour/alpha).
                const float xLeft  = x0;
                const float xRight = x0 + 4.0f * laneW + 3.0f * dividerW; // HF lane right edge
                g.drawLine (xLeft,  y1, xRight, y1, 1.0f); // top
                g.drawLine (xLeft,  y2, xRight, y2, 1.0f); // bottom
                g.drawLine (xLeft,  y1, xLeft,  y2, 1.0f); // left
                g.drawLine (xRight, y1, xRight, y2, 1.0f); // right
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
                g.setColour (topZoneColor);
                g.fillRect (filterBar);
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

    // ===== PHASE 7: SSL KNOB LOCK (deterministic geometry; no gradients/images) =====
    // Helper signature is fixed, but indicator angle must use JUCE's rotaryStart/End angles.
    // We set these immediately before calling drawSSLKnob() from drawRotarySlider().
    static float gRotaryStartAngleRad = 0.0f;
    static float gRotaryEndAngleRad   = juce::MathConstants<float>::twoPi;

    static inline int unitPxFromScaleKey (float scaleKey)
    {
        // unitPx = clamp(round(scaleKey), 1, 2)
        return juce::jlimit (1, 2, juce::roundToInt (scaleKey));
    }

    // PHASE 7 — SSL KNOB RENDERER (LOCKED)
    // Visual gate passed: manufactured hardware at idle
    // Do not modify without reopening Phase 7
    static void drawSSLKnob (juce::Graphics& g, juce::Rectangle<float> b, float value01, float scaleKey)
    {
        if (b.isEmpty())
            return;

        // 0) Precompute geometry (no drawing)
        const float physicalScale = juce::jmax (1.0f, (float) g.getInternalContext().getPhysicalPixelScaleFactor());

        const float size = juce::jmin (b.getWidth(), b.getHeight());
        const float R = 0.5f * size;

        const auto cRaw = b.getCentre();
        const float cx = UIStyle::Snap::snapPx (cRaw.x, physicalScale);
        const float cy = UIStyle::Snap::snapPx (cRaw.y, physicalScale);

        // Geometry ratios (locked)
        const float rOuter      = 1.00f * R;
        const float rSkirtInner = 0.86f * R;
        const float rTopOuter   = 0.86f * R;
        const float rTopInner   = 0.62f * R;
        const float rCap        = 0.26f * R; // locked

        // Strokes (scale-aware, clamped)
        const float px1 = (float) unitPxFromScaleKey (scaleKey);
        // Strengthen tooling break: +1px (scale-aware, crisp)
        const float breakStroke = juce::jlimit (2.0f, 3.0f, px1 + 1.0f);

        auto circleBounds = [] (float ccx, float ccy, float rr)
        {
            return juce::Rectangle<float> (ccx - rr, ccy - rr, rr * 2.0f, rr * 2.0f);
        };

        auto snapEllipse = [&] (juce::Rectangle<float> rf)
        {
            const float x1 = UIStyle::Snap::snapPx (rf.getX(), physicalScale);
            const float y1 = UIStyle::Snap::snapPx (rf.getY(), physicalScale);
            const float x2 = UIStyle::Snap::snapPx (rf.getRight(), physicalScale);
            const float y2 = UIStyle::Snap::snapPx (rf.getBottom(), physicalScale);
            return juce::Rectangle<float> (x1, y1, x2 - x1, y2 - y1);
        };

        // Tones (grayscale only)
        // - Skirt clearly darker than top (dominant mass)
        // - Break ring darker than skirt (crisp separation)
        const juce::Colour skirtCol     = juce::Colour::fromRGB (34, 34, 34);       // darker skirt (more dominance)
        const juce::Colour breakCol     = juce::Colour::fromRGB (18, 18, 18);       // darker than skirt
        const juce::Colour topCol       = juce::Colour::fromRGB (72, 72, 72);       // lighter top (bigger step vs skirt)
        const juce::Colour innerRingCol = juce::Colour::fromRGB (34, 34, 34);       // darker than top
        const juce::Colour capCol       = juce::Colour::fromRGB (46, 46, 46);       // slightly darker than top

        // Snapped bounds
        const auto outerB = snapEllipse (circleBounds (cx, cy, rOuter));
        const auto topB   = snapEllipse (circleBounds (cx, cy, rTopOuter));
        const auto innerB = snapEllipse (circleBounds (cx, cy, rTopInner));
        const auto capB   = snapEllipse (circleBounds (cx, cy, rCap));

        // 1) Skirt fill (dark)
        g.setColour (skirtCol);
        g.fillEllipse (outerB);

        // 2) Break ring stroke at 0.86R (crisp)
        {
            const auto breakB = snapEllipse (circleBounds (cx, cy, rSkirtInner));
            g.setColour (breakCol);
            g.drawEllipse (breakB, breakStroke);
        }

        // 3) Top disc fill (slightly lighter)
        g.setColour (topCol);
        g.fillEllipse (topB);

        // 4) Inner ring stroke at 0.62R
        g.setColour (innerRingCol);
        g.drawEllipse (innerB, px1);

        // 5) Directional edge grammar ONLY via edge strips (global top-left)
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;
            clip.addEllipse (outerB);
            g.reduceClipRegion (clip);

            const float arcR = rOuter - (px1 * 0.75f);

            auto strokeArcDeg = [&] (float degStart, float degEnd, juce::Colour col, float alpha)
            {
                const float a0 = juce::degreesToRadians (degStart);
                const float a1 = juce::degreesToRadians (degEnd);
                juce::Path p;
                p.addCentredArc (cx, cy, arcR, arcR, 0.0f, a0, a1, true);
                g.setColour (col.withAlpha (alpha));
                g.strokePath (p, juce::PathStrokeType (px1, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            };

            // Exactly one highlight arc (top-left) and one occlusion arc (bottom-right)
            // Break lighting symmetry: tighter spans, more directional bias (alpha caps unchanged).
            const float hiA  = juce::jmin (UIStyle::highlightAlphaMax, 0.10f); // slightly reduced skirt lighting
            const float occA = juce::jmin (UIStyle::occlusionAlphaMax, 0.16f);

            strokeArcDeg (175.0f, 265.0f, UIStyle::Colors::foreground, hiA); // top-left (tighter)
            strokeArcDeg ( -5.0f,  85.0f, juce::Colours::black,        occA); // bottom-right (tighter)
        }

        // 6) Center cap fill + 1px cap stroke
        g.setColour (capCol);
        g.fillEllipse (capB);
        g.setColour (juce::Colours::black.withAlpha (0.14f));
        g.drawEllipse (capB, px1);

        // 7) Tooling inflection ring: subtle ring 5–8px inside top edge (low contrast)
        {
            const float insetNom = 6.0f * scaleKey;
            float inset = juce::jlimit (5.0f * scaleKey, 8.0f * scaleKey, insetNom);
            inset = juce::jmin (inset, rTopOuter - (px1 * 2.0f)); // bounds safety for small knobs

            const auto inflectB = snapEllipse (circleBounds (cx, cy, rTopOuter - inset));
            g.setColour (juce::Colours::black.withAlpha (0.035f));
            g.drawEllipse (inflectB, px1);
        }

        // 8) Indicator: UNDERSTROKE dark then MAIN stroke light, square/butt caps
        {
            const float v = juce::jlimit (0.0f, 1.0f, value01);
            const float angleRad = gRotaryStartAngleRad + v * (gRotaryEndAngleRad - gRotaryStartAngleRad);

            const float r0 = 0.28f * R;
            const float r1 = 0.78f * R;
            auto p0 = juce::Point<float> (cx, cy).getPointOnCircumference (r0, angleRad);
            auto p1 = juce::Point<float> (cx, cy).getPointOnCircumference (r1, angleRad);
            p0 = UIStyle::Snap::snapPoint (p0, physicalScale);
            p1 = UIStyle::Snap::snapPoint (p1, physicalScale);

            juce::Path indicator;
            indicator.startNewSubPath (p0);
            indicator.lineTo (p1);

            // Thickness rule:
            // - main stroke >= 2.0f * scaleKey
            // - under-stroke thicker than main
            const float mainW  = juce::jmax (2.0f * scaleKey, 2.0f);
            const float underW = mainW + px1;

            g.setColour (UIStyle::Colors::knobIndicatorUnderStroke.withAlpha (0.45f));
            g.strokePath (indicator, juce::PathStrokeType (underW, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

            g.setColour (UIStyle::Colors::knobIndicator.withAlpha (0.95f));
            g.strokePath (indicator, juce::PathStrokeType (mainW, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        }
    }
}

// ===== Value popup helper (cpp-only) =====
static inline juce::String popupTextFor (juce::Slider& s)
{
    // Uses JUCE's value→text conversion (suffix/decimals) if you set it.
    return s.getTextFromValue (s.getValue());
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
    juce::Slider&)
{
    const float scaleKey = editor.getScaleKeyActive();
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    bounds = bounds.reduced (juce::jmax (1.0f, 1.0f * scaleKey));
    gRotaryStartAngleRad = rotaryStartAngle;
    gRotaryEndAngleRad = rotaryEndAngle;
    drawSSLKnob (g, bounds, sliderPos, scaleKey);
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

    // Wire readout behavior for each slider
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
        hpfFreq.getBounds(),
        lpfFreq.getBounds(),
        inputMeter.getBounds(),
        outputMeter.getBounds(),
        physicalScale);

    // ---- Global border ----
    // Stage 5.5 lock: outside colored lanes is neutral black; do not draw non-black plate chrome here.

    // Stage 1: Tier 3 wells only (no group panels)
    drawTier3Well (g, lfFreq.getBounds(),  physicalScale);
    drawTier3Well (g, lfGain.getBounds(),  physicalScale);
    drawTier3Well (g, lmfFreq.getBounds(), physicalScale);
    drawTier3Well (g, lmfGain.getBounds(), physicalScale);
    drawTier3Well (g, lmfQ.getBounds(),    physicalScale);
    drawTier3Well (g, hmfFreq.getBounds(), physicalScale);
    drawTier3Well (g, hmfGain.getBounds(), physicalScale);
    drawTier3Well (g, hmfQ.getBounds(),    physicalScale);
    drawTier3Well (g, hfFreq.getBounds(),  physicalScale);
    drawTier3Well (g, hfGain.getBounds(),  physicalScale);
    drawTier3Well (g, hpfFreq.getBounds(), physicalScale);
    drawTier3Well (g, lpfFreq.getBounds(), physicalScale);
    drawTier3Well (g, inTrim.getBounds(),  physicalScale);
    drawTier3Well (g, outTrim.getBounds(), physicalScale);

    // ---- Keep your existing Phase 3.3 text system (headers/legends/ticks) ----
    // Phase 2: Discrete font ladder by scaleKey
    const auto& titleFont  = UIStyle::FontLadder::titleFont (scaleKey);
    const auto& headerFont = UIStyle::FontLadder::headerFont (scaleKey);
    const auto& microFont  = UIStyle::FontLadder::microFont (scaleKey);
    const float hairlineStroke = UIStyle::StrokeLadder::hairlineStroke (scaleKey);

    auto drawHeaderAbove = [&g, &headerFont, kHeaderA, physicalScale] (const char* txt, juce::Rectangle<int> b, int yOffset)
    {
        g.setColour (UIStyle::Colors::foreground.withAlpha (kHeaderA));
        g.setFont (headerFont);
        // Phase 2: Snap baseline Y
        const float snappedY = UIStyle::Snap::snapPx ((float) (b.getY() + yOffset), physicalScale);
        g.drawFittedText (txt, b.getX(), (int) snappedY, b.getWidth(), 12, juce::Justification::centred, 1);
    };

    auto drawLegendBelow = [&g, &microFont, kMicroA, physicalScale] (const char* txt, juce::Rectangle<int> b, int yOffset)
    {
        g.setColour (UIStyle::Colors::foreground.withAlpha (kMicroA));
        g.setFont (microFont);
        // Phase 2: Snap baseline Y
        const float snappedY = UIStyle::Snap::snapPx ((float) (b.getBottom() + yOffset), physicalScale);
        g.drawFittedText (txt, b.getX(), (int) snappedY, b.getWidth(), 12, juce::Justification::centred, 1);
    };

    auto drawTick = [&g, kTickA, physicalScale, hairlineStroke] (juce::Rectangle<int> b, int yOffset)
    {
        const float cx = UIStyle::Snap::snapPx ((float) b.getCentreX(), physicalScale);
        const float y0 = UIStyle::Snap::snapPx ((float) (b.getY() + yOffset), physicalScale);
        const float y1 = UIStyle::Snap::snapPx ((float) (b.getY() + yOffset + 6), physicalScale);
        g.setColour (UIStyle::Colors::foreground.withAlpha (kTickA));
        g.drawLine (cx, y0, cx, y1, hairlineStroke);
    };

    auto drawColLabel = [&g, &headerFont, kHeaderA, physicalScale] (const char* txt, juce::Rectangle<int> columnBounds, int y)
    {
        g.setColour (UIStyle::Colors::foreground.withAlpha (kHeaderA));
        g.setFont (headerFont);
        // Phase 2: Snap baseline Y
        const float snappedY = UIStyle::Snap::snapPx ((float) y, physicalScale);
        g.drawFittedText (txt, columnBounds.getX(), (int) snappedY, columnBounds.getWidth(), 14, juce::Justification::centred, 1);
    };

    // Title (centered inside header plate) - Phase 2: Snap baseline Y
    g.setColour (UIStyle::Colors::foreground.withAlpha (kTitleA));
    g.setFont (titleFont);
    // Title stays at existing bounds (no panel dependency)
    {
        // Reuse headerZone bounds without drawing a header plate
        const int inset = 16;
        auto headerFW = fullWidthFrom (assetSlots.editor, assetSlots.headerZone, inset);
        if (! headerFW.isEmpty())
        {
            auto titleRect = headerFW.withTrimmedTop (6).withHeight (24);
            const float snappedY = UIStyle::Snap::snapPx ((float) titleRect.getY(), physicalScale);
            titleRect.setY ((int) snappedY);
            g.drawText ("COMPASS EQ", titleRect, juce::Justification::centred, false);
        }
    }

    // Column labels (driven by slot unions)
    const int topY = juce::jmin (assetSlots.colLF.getY(),
                                assetSlots.colLMF.getY(),
                                assetSlots.colHMF.getY(),
                                assetSlots.colHF.getY());
    const int bandLabelY = topY - 18;

    drawColLabel ("LF",  assetSlots.colLF,  bandLabelY);
    drawColLabel ("LMF", assetSlots.colLMF, bandLabelY);
    drawColLabel ("HMF", assetSlots.colHMF, bandLabelY);
    drawColLabel ("HF",  assetSlots.colHF,  bandLabelY);

    // Headers
    drawHeaderAbove ("HPF", hpfFreq.getBounds(), -16);
    drawHeaderAbove ("LPF", lpfFreq.getBounds(), -16);

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

        g.setColour (UIStyle::Colors::foreground.withAlpha (kHeaderA));
        g.setFont (f);
        g.drawFittedText ("IN",  inLabel,  juce::Justification::centred, 1);
        g.drawFittedText ("OUT", outLabel, juce::Justification::centred, 1);
    }

    // Legends
    drawLegendBelow ("FREQ", lfFreq.getBounds(),  2);
    drawLegendBelow ("GAIN", lfGain.getBounds(),  2);

    drawLegendBelow ("FREQ", lmfFreq.getBounds(), 2);
    drawLegendBelow ("GAIN", lmfGain.getBounds(), 2);
    drawLegendBelow ("Q",    lmfQ.getBounds(),    2);

    drawLegendBelow ("FREQ", hmfFreq.getBounds(), 2);
    drawLegendBelow ("GAIN", hmfGain.getBounds(), 2);
    drawLegendBelow ("Q",    hmfQ.getBounds(),    2);

    drawLegendBelow ("FREQ", hfFreq.getBounds(),  2);
    drawLegendBelow ("GAIN", hfGain.getBounds(),  2);

    drawLegendBelow ("FREQ", hpfFreq.getBounds(), 2);
    drawLegendBelow ("FREQ", lpfFreq.getBounds(), 2);

    // (No "TRIM" legend under the bottom trim knobs per spec.)

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
    // Editor: 760 x 420
    // Grid base: 8 (all integer placement)
    // Zones: 64 / 72 / 200 / 84 (sum 420)
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
    const int z3H = 200;

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
    const int filterSpacing = 32;
    const int filtersTotalW = filterKnob + filterSpacing + filterKnob; // 128

    const int filtersStartX = marginL + ((usableW - filtersTotalW) / 2); // 316
    // Stage 5.4b (LOCKED): place HPF/LPF knob centers using topZoneRect:
    // HPF_LPF_CENTER_Y = topZoneRect.y + (topZoneRect.height * 0.40)
    // Filter bar Y geometry lock:
    //   - labelTopPad = 28 (header yOffset=-16, height=12)
    //   - padTop = 10, padBottom = 10
    //   - barHeight = filterKnob + labelTopPad + padTop + padBottom = 96
    constexpr int labelTopPad = 28;
    constexpr int padTop = 10;
    constexpr int padBottom = 10;
    constexpr int filterBarH = filterKnob + labelTopPad + padTop + padBottom; // 96
    const int filterBarTop = z2Y - (16 + padTop); // top of label area (z2Y - 16) minus padTop
    const int filterBarCenterY = filterBarTop + (int) std::lround ((float) filterBarH * 0.40f);
    const int filtersY = filterBarCenterY - (filterKnob / 2);

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
    // LMF/HMF stack: 48 + 16 + 56 + 16 + 40 = 176 => top offset (200-176)/2 = 12
    const int stack3Top = z3Y + 12;
    const int lmfFreqY  = stack3Top;               // 148
    const int lmfGainY  = lmfFreqY + 48 + 16;      // 212
    const int lmfQY     = lmfGainY + 56 + 16;      // 284

    // LF/HF stack: 48 + 20 + 56 = 124 => top offset (200-124)/2 = 38
    const int stack2Top = z3Y + 38;
    const int lfFreqY   = stack2Top;               // 174
    const int lfGainY   = lfFreqY + 48 + 20;       // 242

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
        constexpr int trimSize   = 56;
        constexpr int bypassW    = 140;
        constexpr int bypassH    = 26; // STAGE: SSL BYPASS latch (LOCKED)

        const int cy = zone4.getCentreY();

        // BYPASS centered
        const auto bypassBounds = juce::Rectangle<int> (0, 0, bypassW, bypassH)
                                    .withCentre ({ zone4.getCentreX(), cy });
        globalBypass.setBounds (bypassBounds);

        // Trims: symmetric around bypass, keep >= 32px spacing (4g)
        constexpr int minGapToBypass = 32;

        const int leftTrimCx  = bypassBounds.getX() - minGapToBypass - (trimSize / 2);
        const int rightTrimCx = bypassBounds.getRight() + minGapToBypass + (trimSize / 2);

        inTrim.setBounds  (juce::Rectangle<int> (0, 0, trimSize, trimSize).withCentre ({ leftTrimCx,  cy }));
        outTrim.setBounds (juce::Rectangle<int> (0, 0, trimSize, trimSize).withCentre ({ rightTrimCx, cy }));
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
