# PHASE 2A — OPTIONAL ADDENDUM (NON-AUTHORITATIVE)

Status: OPTIONAL GUIDANCE ONLY  
Authority: NONE  
This document does not modify, override, or reinterpret any Constitution, Checklist, or Lock file.
If any statement here conflicts with governing documents, this document is ignored.

---

## 1) Filter Slope Implementation (Allowed Technique / Locked Numbers)

Allowed technique:
- JUCE `dsp::IIR::Filter` and `dsp::IIR::Coefficients` may be used.

Locked slopes (must match governing spec):
- HPF = 18 dB/oct (typically 3rd order)
- LPF = 12 dB/oct (typically 2nd order)

Do not substitute 12/24/36 dB slopes based on preference.

---

## 2) Band Shelf Order Note (Phase 2B Only)

Shelf order MUST follow Constitution if already specified.
If Constitution does not specify shelf order, pick one intentionally and treat it as locked for V1.

No ad-hoc code decisions.

---

## 3) Parameter Smoothing Guidance (Implementation Hint)

Recommended:
- Use `juce::SmoothedValue` per parameter.

Guardrails:
- RT-safe coefficient updates only.
- If using cross-thread change flags, use atomic-only (no locks, no allocations).

Alternative acceptable:
- Recompute coefficients once per block during Phase 2A if RT-safe.

---

## 4) Offline vs Realtime Render Comparison (Manual Test Only)

Optional manual test to detect:
- denormals
- state inconsistencies
- automation weirdness

This must remain a manual procedure and may not alter architecture.

---

## 5) UI in Phase 2A (Optional)

Optional: Add only these controls for testability:
- Input Trim
- Output Trim
- HPF Frequency
- LPF Frequency

Forbidden in 2A:
- analyzers
- curve displays
- menus
- tooltips
- additional buttons or overlays
