# Typography Ladder

## Overview
Strict typography system calibrated to read as equipment labeling, not UI copy.
- **Primary reference**: SSL EQ sections
- **Secondary reference**: GML 8200/8300

---

## 1) Typography Philosophy (HARD LOCK)

### Core Principles
- **Neutral**: No emotional tone, no warmth, no coldness
- **Technical**: Reads as engineering documentation, not marketing
- **Utilitarian**: Function over form, clarity over style

### Visual Identity
- **Reads as labeling stamped on equipment**: Text should appear as if physically applied to hardware
- **No friendliness**: Reject conversational tone, playful language, or "helpful" phrasing
- **No personality**: Reject brand voice, character, or distinctive style
- **No "UI voice"**: Reject modern UI conventions that prioritize user comfort over technical accuracy

### Forbidden Approaches
- ❌ Friendly, approachable, or welcoming tone
- ❌ Personality-driven typography
- ❌ Modern UI conventions (rounded corners on text, playful spacing)
- ❌ Decorative or expressive typography

---

## 2) Font Family Selection

### Chosen Family
**macOS System Font (SF Pro)** — Regular and Medium weights only

### Rationale
- **System font fits references**: SSL and GML hardware use utilitarian sans-serif that reads as stamped/engraved
- **Neutral appearance**: SF Pro in regular/medium weights reads as technical labeling, not UI copy
- **Consistent with platform**: Native macOS font ensures crisp rendering and system integration
- **No decorative elements**: SF Pro lacks personality quirks that would undermine equipment aesthetic

### Font Mixing Policy
- **STRICTLY PROHIBITED**: No mixing of font families
- **Single family only**: All text uses SF Pro (or system font equivalent)
- **No exceptions**: Even for special cases, use the same family with different weight/size

### Alternative Consideration
If SF Pro proves too "UI-like" in practice, consider:
- **Helvetica Neue** (more neutral, less modern)
- **Arial** (utilitarian, widely available)
- **Roboto** (if cross-platform consistency required)

**Decision**: Start with SF Pro, evaluate against hardware references, adjust only if necessary.

---

## 3) Hierarchy Ladder (MANDATORY)

### Tier 1: Title
**Purpose**: Plugin name / main identifier

- **Font size**: 18pt (logical) / 36pt @ 2.00 scale
- **Weight**: Medium (500)
- **Alpha**: 0.90
- **Case**: UPPERCASE
- **Alignment**: Center (if centered in header zone) or Left (if left-aligned per hardware convention)
- **Tracking**: 0 (default, no adjustment)

**Example**: "COMPASS EQ"

### Tier 2: Section Headers
**Purpose**: Bands / functional zones (e.g., "LF", "LMF", "HMF", "HF", "FILTERS", "TRIM")

- **Font size**: 12pt (logical) / 24pt @ 2.00 scale
- **Weight**: Medium (500)
- **Alpha**: 0.75
- **Case**: UPPERCASE
- **Alignment**: Center (hardware convention: labels centered above controls)
- **Tracking**: 0 (default)

**Example**: "LF", "LMF", "HMF", "HF"

### Tier 3: Control Labels
**Purpose**: Control identifiers (e.g., "FREQ", "GAIN", "Q", "TRIM")

- **Font size**: 10pt (logical) / 20pt @ 2.00 scale
- **Weight**: Regular (400)
- **Alpha**: 0.75
- **Case**: UPPERCASE
- **Alignment**: Center (hardware convention: labels centered below controls)
- **Tracking**: 0 (default)

**Example**: "FREQ", "GAIN", "Q", "TRIM"

### Tier 4: Micro Labels
**Purpose**: Units, Hz/kHz, secondary identifiers, tick marks

- **Font size**: 8pt (logical) / 16pt @ 2.00 scale
- **Weight**: Regular (400)
- **Alpha**: 0.45
- **Case**: Mixed (follow hardware convention: "Hz", "kHz", "-12", "+12")
- **Alignment**: Center or Left (per hardware convention)
- **Tracking**: 0 (default)

**Example**: "Hz", "kHz", "-12", "+12", "dB"

### Hierarchy Summary Table
| Tier | Purpose | Size | Weight | Alpha | Case | Alignment |
|------|---------|------|--------|-------|------|-----------|
| Title | Plugin name | 18pt | Medium | 0.90 | UPPER | Center/Left |
| Section | Bands/zones | 12pt | Medium | 0.75 | UPPER | Center |
| Control | Labels | 10pt | Regular | 0.75 | UPPER | Center |
| Micro | Units/ticks | 8pt | Regular | 0.45 | Mixed | Center/Left |

---

## 4) Baseline Discipline

### Pixel Grid Snapping (MANDATORY)
- **All text baselines must snap to device pixel grid**: Use `UIStyle::Snap::snapPx()` on baseline Y coordinate
- **No vertical optical nudging per-label**: Do not adjust baseline per label for "visual balance"
- **One baseline offset rule per tier**: Each tier has a single, consistent baseline offset

### Baseline Rules Per Tier
- **Title**: Baseline Y = snapped Y coordinate (no offset)
- **Section**: Baseline Y = snapped Y coordinate (no offset)
- **Control**: Baseline Y = snapped Y coordinate (no offset)
- **Micro**: Baseline Y = snapped Y coordinate (no offset)

### Implementation
- Compute text baseline Y position
- Apply `UIStyle::Snap::snapPx(baselineY, physicalScale)`
- Use snapped Y for all text drawing in that tier
- **No exceptions**: Even if text appears slightly misaligned, maintain pixel grid discipline

---

## 5) Spacing & Tracking Rules

### Default Tracking
- **Default tracking = 0**: No letter spacing adjustment by default
- **Very slight negative tracking allowed**: Only if hardware reference shows tighter spacing (e.g., -0.5pt max)
- **No positive tracking**: Never add space between letters

### Explicit Rejections
- ❌ **"Airy" spacing**: Reject generous letter spacing that feels modern/UI-like
- ❌ **Decorative letter spacing**: Reject any tracking that draws attention to typography itself
- ❌ **Variable tracking**: Reject different tracking per label (consistency is mandatory)

### Word Spacing
- **Default word spacing**: System default (no adjustment)
- **No manual word spacing**: Let system handle word breaks and spacing

### Line Height
- **Tight line height**: Use system default or slightly tighter (1.0–1.1× font size)
- **No generous leading**: Reject "airy" line spacing that feels UI-like

---

## 6) Explicit Rejections (HARD LOCK)

### Font Family Rejections
- ❌ **Rounded or friendly UI fonts**: No SF Rounded, no system rounded variants
- ❌ **Decorative fonts**: No serif, no script, no display fonts
- ❌ **Ultra-modern fonts**: No fonts with personality quirks

### Weight Rejections
- ❌ **Ultra-thin modern weights**: No 100, 200, or 300 weights
- ❌ **Ultra-bold weights**: No 700+ weights (equipment labeling is not bold)
- **Allowed weights only**: Regular (400) and Medium (500)

### Tracking Rejections
- ❌ **Excessive tracking**: No tracking > 1pt or < -1pt
- ❌ **Decorative tracking**: No tracking that draws attention

### Numeral Rejections
- ❌ **Decorative numerals**: No old-style figures, no tabular figures with personality
- **Use default numerals**: System default numeral style only

### Alignment Rejections
- ❌ **Center-aligned labels where hardware convention dictates otherwise**: Follow hardware reference alignment
- **Example**: If SSL/GML hardware shows left-aligned frequency labels, use left alignment (not center)

### Style Rejections
- ❌ **Italic**: No italic text (equipment labeling is never italic)
- ❌ **Oblique**: No oblique/slanted text
- ❌ **Underline**: No underlines (not hardware convention)
- ❌ **Strikethrough**: No strikethrough (not hardware convention)

---

## 7) Implementation Notes

### Mapping to UIStyle Tokens
- **Font selection**: Use `UIStyle::FontLadder::titleFont(scaleKey)`, `headerFont(scaleKey)`, `microFont(scaleKey)`
- **Alpha values**: Use `UIStyle::TextAlpha::title` (0.90), `header` (0.75), `micro` (0.45)
- **Baseline snapping**: Use `UIStyle::Snap::snapPx(baselineY, physicalScale)` in all text drawing
- **Font sizes**: Defined in `UIStyle::FontLadder` functions, scale with `scaleKey`

### Current Implementation Status
- `UIStyle::FontLadder` already provides `titleFont()`, `headerFont()`, `microFont()` functions
- Fonts return `const juce::FontOptions&` from prebuilt tables (1.00 and 2.00 scale keys)
- Text drawing in `renderStaticLayer()` uses these ladders
- Baseline snapping is implemented via `UIStyle::Snap::snapPx()`

### Visual-Only Scope
- **This ladder is visual-only**
- **Does NOT mandate layout changes yet**
- Layout geometry remains frozen per Phase Lock
- Typography appearance (size, weight, alpha, case) can be refined within existing bounds

### Future Considerations
- When layout is unlocked, text positioning should follow hardware alignment conventions
- Text should align with control centers (hardware convention)
- Micro labels should align with tick marks or units (hardware convention)
- All text should respect the tier hierarchy visually

### Reference Checklist
- [ ] SSL EQ sections: typography style observed
- [ ] GML 8200/8300: typography style referenced
- [ ] All tiers use correct size/weight/alpha
- [ ] All text uses UPPERCASE (except micro tier)
- [ ] Baselines snap to pixel grid
- [ ] Tracking is zero or very slight negative
- [ ] No decorative elements
- [ ] Alignment follows hardware convention

---

## Final Notes

This typography system is **PRESCRIPTIVE AND FINAL**. It defines the exact visual language for all text in the plugin. Any deviation from this ladder must be explicitly justified against hardware references (SSL/GML) and documented.

**No exceptions without hardware reference justification.**
