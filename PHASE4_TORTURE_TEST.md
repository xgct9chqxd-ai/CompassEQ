# Phase 4 Torture Test Script - Studio One

## Setup
1. Open Studio One
2. Create a new project
3. Load audio into a track (or use built-in test tone)

## Test Sequence

### Test 1: Multi-Instance Load/Unload
1. Insert **Compass EQ** on track 1
2. Insert **Compass EQ** on track 2
3. Insert **Compass EQ** on track 3
4. Open all 3 editor windows
5. Close all 3 editor windows
6. Remove plugin from track 3 (while audio is playing)
7. Remove plugin from track 2 (while audio is playing)
8. Remove plugin from track 1 (while audio is playing)
9. **PASS**: No crash, no hang, no error dialogs

### Test 2: Rapid Open/Close
1. Insert **Compass EQ** on track 1
2. Open editor
3. Close editor
4. Open editor
5. Close editor
6. Repeat 10 times rapidly
7. **PASS**: No crash, editor opens/closes cleanly each time

### Test 3: Meters Active During Teardown
1. Insert **Compass EQ** on track 1
2. Open editor
3. Play audio (meters should be active/animating)
4. While meters are animating, close editor
5. While meters are animating, remove plugin from track
6. **PASS**: No crash, no timer errors, clean teardown

### Test 4: Multiple Instances + Rapid Operations
1. Insert **Compass EQ** on 5 tracks simultaneously
2. Open all 5 editors
3. Play audio (all meters active)
4. Rapidly close/open editors randomly
5. Remove plugins while editors are open
6. Remove plugins while editors are closed
7. **PASS**: No crash, no dead UI, all instances clean up

### Test 5: Reopen After Teardown
1. Insert **Compass EQ** on track 1
2. Open editor, adjust some knobs
3. Close editor
4. Remove plugin
5. Re-insert **Compass EQ** on same track
6. Open editor
7. **PASS**: Editor opens fresh, no stale state, knobs work

### Test 6: Scale Change During Active Use
1. Insert **Compass EQ** on track 1
2. Open editor
3. Play audio (meters active)
4. Move plugin window between Retina and non-Retina display (if available)
5. Or: Change display scaling while editor is open
6. **PASS**: No crash, cache rebuilds cleanly, UI remains responsive

## PASS Criteria
- ✅ No crashes
- ✅ No "weird after reopen" (stale UI, dead controls)
- ✅ No dead UI (frozen meters, unresponsive knobs)
- ✅ No error dialogs
- ✅ Clean teardown (no console errors about timers/async)
- ✅ All instances independent (no cross-contamination)

## Failure Indicators
- ❌ Crash on close
- ❌ Crash on remove
- ❌ Editor won't reopen
- ❌ Meters frozen after reopen
- ❌ Knobs don't respond after reopen
- ❌ Console errors about timers/AsyncUpdater
- ❌ Memory leaks (check Activity Monitor)

