# Plates & Wells Recipe

## Overview
Visual specification for plate/well depth hierarchy and material language.
- **Primary reference**: SSL EQ sections
- **Secondary reference**: GML 8200/8300

---

## 1) Depth Tier System (HARD LOCK)

Three-tier depth hierarchy, strictly enforced:

### Tier 1: Chassis
- Outermost container (editor background)
- Deepest visual layer
- Provides structural foundation

### Tier 2: Plates / Sections
- Functional grouping containers
- Recessed into chassis
- Contains related controls (e.g., filter bands, trim section)

### Tier 3: Wells / Insets
- Individual control containers
- Recessed into plates
- Contains single controls (knobs, meters)

### Visual Diagram
```
┌─────────────────────────────────┐
│  TIER 1: CHASSIS                │
│  ┌───────────────────────────┐  │
│  │  TIER 2: PLATE            │  │
│  │  ┌───────────┐            │  │
│  │  │ TIER 3:   │            │  │
│  │  │ WELL      │            │  │
│  │  └───────────┘            │  │
│  └───────────────────────────┘  │
└─────────────────────────────────┘
```

### Forbidden
- ❌ **NO floating UI rectangles** (all elements must belong to a tier)
- ❌ NO elements that appear to float above the chassis
- ❌ NO elements without clear tier assignment

---

## 2) Edge System (HARD LOCK)

Single radius family derived from one mathematical rule (not eyeballed).

### Radius Rule
All radii derive from a base unit (e.g., 4px or 8px grid):
- **Chassis R** = base radius
- **Plate R** = base radius × 0.75 (or similar ratio)
- **Well R** = base radius × 0.5 (or similar ratio)

### Stroke Logic
- **One stroke logic per tier**: Each tier uses identical stroke width and alpha
- **Identical highlight + occlusion behavior within each tier**: All elements at the same tier share the same visual treatment

### Visual Consistency
- All plates use the same radius ratio
- All wells use the same radius ratio
- Stroke thickness is tier-dependent, not element-dependent

---

## 3) Surface Density Rules

### Controls Must Visually Belong to Plates
- Controls (knobs, meters) must appear integrated into their plate
- No orphaned controls floating in negative space
- Visual connection between control and plate must be clear

### Plates Must Appear Functionally Occupied
- Plates should not appear empty or sparse
- Controls should fill plates appropriately (not too tight, not too loose)
- Functional density reinforces purpose

### Negative Space Policy
- Negative space allowed **only** to reinforce hierarchy
- Negative space **never** by default
- Every space should have a purpose (separation, grouping, emphasis)

---

## 4) Material Language

### Allowed Materials

#### Powder-Coated Metal
- **Chassis**: Powder-coated metal finish
- **Plates**: Powder-coated metal finish (same or slightly different tone)
- Matte, non-reflective surface
- Subtle texture, no gloss

#### Injection-Molded Polymer
- **Controls**: Injection-molded polymer (knobs, buttons)
- Matte plastic appearance
- Slightly different reflectivity than metal (subtle)

### Forbidden Effects (EXPLICIT)
- ❌ **Drop shadows** (no shadows outside element bounds)
- ❌ **Glass effects** (no transparency, no glassmorphism)
- ❌ **HDR effects** (no bloom, no overexposed highlights)
- ❌ **PNG grain** (no texture overlays, no noise patterns)
- ❌ **Chrome/chrome-like** (no metallic shine, no specular highlights)
- ❌ **Gradients on edges** (no soft edge gradients that suggest depth via blur)

### Visual Language
- **Matte surfaces only**
- **Hard edges** (no soft shadows, no blur)
- **Depth via occlusion** (darker areas in recessed regions)
- **Subtle highlights** (top edges only, very low alpha)

---

## 5) Implementation Notes

### Mapping to UIStyle Tokens
- **Plate radius**: Use `UIStyle::Plate::radius` (or equivalent)
- **Well radius**: Derive from plate radius using tier ratio
- **Stroke widths**: Use `UIStyle::StrokeLadder` per tier
- **Colors**: Use `UIStyle::Colors::*` for chassis/plate/well
- **Alphas**: Use `UIStyle::UIAlpha::*` for stroke/highlight/occlusion

### Existing Plate Draw Helpers
- `drawPlate()` function in `PluginEditor.cpp` handles plate rendering
- Current implementation uses `PlateStyle` struct for fill/stroke/radius
- Recipe should inform future `PlateStyle` refinements

### Visual-Only Scope
- **This recipe is visual-only**
- **Does NOT mandate layout changes yet**
- Layout geometry remains frozen per Phase Lock
- Visual appearance (colors, radii, strokes) can be refined within existing bounds

### Future Considerations
- When layout is unlocked, plate/well bounds should follow tier hierarchy
- Wells should be visually inset into plates
- Plates should be visually inset into chassis
- All radii should follow the mathematical ratio rule

---

## Reference Checklist
- [ ] SSL EQ sections: depth hierarchy observed
- [ ] GML 8200/8300: material language referenced
- [ ] All tiers have clear visual separation
- [ ] No floating elements
- [ ] Radius family is mathematically consistent
- [ ] Forbidden effects are avoided
- [ ] Surface density rules are followed
