# Annotated Idle Screenshot Analysis

## Overview
Analysis of idle screenshot against Visual Art Direction v1.2 rules.
- **Screenshot**: `CompassEQ_idle.png` (to be analyzed)
- **Reference documents**: `PLATES_WELLS_RECIPE.md`, `TYPO_LADDER.md`

---

## 1) Screenshot Context

### Host Used
<!-- Document the DAW/host used for the screenshot -->
- **Host**: [To be filled]
- **Version**: [To be filled]

### Display Context
<!-- Document the display setup -->
- **Display**: [Internal / External]
- **Monitor**: [To be filled]
- **Resolution**: [To be filled]

### Scale
<!-- Document the scale factor if known -->
- **Physical scale**: [To be filled]
- **Scale key**: [1.00 / 2.00 / Other]
- **Retina**: [Yes / No]

---

## 2) Plate Occupancy Analysis

### Plate Inventory
<!-- List each plate/section and evaluate occupancy -->

#### Plate 1: [Zone Name]
- **Location**: [Description]
- **Occupancy status**: [Functionally occupied / Borderline / Empty]
- **Risk level**: [Low / Medium / High]
- **Notes**: [Observations]

#### Plate 2: [Zone Name]
- **Location**: [Description]
- **Occupancy status**: [Functionally occupied / Borderline / Empty]
- **Risk level**: [Low / Medium / High]
- **Notes**: [Observations]

#### Plate 3: [Zone Name]
- **Location**: [Description]
- **Occupancy status**: [Functionally occupied / Borderline / Empty]
- **Risk level**: [Low / Medium / High]
- **Notes**: [Observations]

### Empty UI Canvas Risk
<!-- Explicitly call out regions that feel like empty UI canvas -->
- **At-risk regions**: [List regions that could feel like empty UI canvas]
- **Risk assessment**: [Low / Medium / High]
- **Reference violation**: [Which rule from PLATES_WELLS_RECIPE.md is at risk?]

---

## 3) Surface Density Analysis

### Density Successes
<!-- Where density succeeds per PLATES_WELLS_RECIPE.md Section 3 -->
- **Region**: [Description]
  - **Why it succeeds**: [Explanation]
  - **Reference alignment**: [How it aligns with hardware references]

- **Region**: [Description]
  - **Why it succeeds**: [Explanation]
  - **Reference alignment**: [How it aligns with hardware references]

### Borderline Density
<!-- Where density may be borderline -->
- **Region**: [Description]
  - **Risk**: [Why it's borderline]
  - **Threshold concern**: [What makes it borderline vs. acceptable?]

### Arbitrary Widget Risk
<!-- Explicitly note if any region could accept arbitrary UI widgets without redesign -->
- **Region**: [Description]
  - **Could accept arbitrary widgets**: [Yes / No]
  - **Risk level**: [Low / Medium / High]
  - **Why**: [Explanation - this violates surface density rules]

---

## 4) Control Mounting Evaluation

### Knob-to-Well Integration
<!-- Do knobs read as installed into wells? -->
- **Knob group**: [Description]
  - **Reads as installed**: [Yes / No / Borderline]
  - **Visual connection**: [Strong / Weak / None]
  - **Notes**: [Observations]

- **Knob group**: [Description]
  - **Reads as installed**: [Yes / No / Borderline]
  - **Visual connection**: [Strong / Weak / None]
  - **Notes**: [Observations]

### Control-to-Plate Belonging
<!-- Do controls visually belong to plates? -->
- **Control group**: [Description]
  - **Visually belongs to plate**: [Yes / No / Borderline]
  - **Orphaned appearance**: [Yes / No]
  - **Notes**: [Observations]

### "Placed on Top" Elements
<!-- Any elements that feel "placed on top" (forbidden per PLATES_WELLS_RECIPE.md) -->
- **Element**: [Description]
  - **Feels placed on top**: [Yes / No]
  - **Tier violation**: [Which tier rule is violated?]
  - **Risk level**: [Low / Medium / High]

---

## 5) Typography Read

### Labeling vs. UI Copy
<!-- Does text read as labeling, not UI copy? -->
- **Overall read**: [Labeling / UI Copy / Borderline]
- **Hardware-grade appearance**: [Yes / No / Borderline]
- **Notes**: [Observations]

### Hierarchy Clarity
<!-- Does hierarchy read instantly? -->
- **Title tier**: [Clear / Unclear / Missing]
- **Section tier**: [Clear / Unclear / Missing]
- **Control tier**: [Clear / Unclear / Missing]
- **Micro tier**: [Clear / Unclear / Missing]
- **Overall hierarchy**: [Instant / Requires effort / Unclear]

### Tier Weight Assessment
<!-- Any tier that feels too light / too friendly? -->
- **Title tier**: [Appropriate / Too light / Too friendly / Other]
- **Section tier**: [Appropriate / Too light / Too friendly / Other]
- **Control tier**: [Appropriate / Too light / Too friendly / Other]
- **Micro tier**: [Appropriate / Too light / Too friendly / Other]

### Typography Violations
<!-- Explicit violations of TYPO_LADDER.md rules -->
- **Violation**: [Description]
  - **Rule violated**: [Which rule from TYPO_LADDER.md?]
  - **Impact**: [Low / Medium / High]

---

## 6) Verdict

### Hardware-Grade Idle Read
- **Verdict**: [PASS / FAIL]

### Structural Risks (Top 1–2 Only)
<!-- If FAIL, list ONLY the top 1–2 structural risks (no solutioning yet) -->

#### Risk #1: [Risk Name]
- **Description**: [What is the risk?]
- **Rule violated**: [Which rule from Visual Art Direction docs?]
- **Impact**: [Why this prevents hardware-grade read]
- **Evidence**: [Where is it visible in screenshot?]

#### Risk #2: [Risk Name] (if applicable)
- **Description**: [What is the risk?]
- **Rule violated**: [Which rule from Visual Art Direction docs?]
- **Impact**: [Why this prevents hardware-grade read]
- **Evidence**: [Where is it visible in screenshot?]

### Analysis Notes
<!-- Additional observations that don't constitute structural risks -->
- [Any other observations that don't rise to the level of structural risks]

---

## Reference Checklist
- [ ] Screenshot analyzed against PLATES_WELLS_RECIPE.md
- [ ] Screenshot analyzed against TYPO_LADDER.md
- [ ] All observations tied to specific rules
- [ ] Verdict is honest (gate, not pep talk)
- [ ] Only top 1–2 structural risks listed (if FAIL)
- [ ] No solutioning included (analysis only)

---

## Analysis Date
- **Date**: [To be filled]
- **Analyzer**: [To be filled]
- **Screenshot version**: [To be filled]

