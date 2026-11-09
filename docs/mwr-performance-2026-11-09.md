# MWR performance investigation, 2026-11-09

## Evidence and limits

The existing i5-12400F report (`bugreport/20261108/overlord-status-latest.txt`,
outside the source repository) contains only 31 stereo transactions. Mean CPU
owner transaction time was 5.697 ms; native left/right owner invocations account
for 2.745 + 2.175 = 4.920 ms (about 86%). Dynamic synchronization was 0.076 +
0.049 ms and native-copy stages 0.350 + 0.100 ms. Copy-lock waits rounded to zero.
This identifies the largest measured CPU stage, not the game's overall CPU
bottleneck or a steady-state benchmark. The invocation includes native rendering,
hooks, HUD work and driver calls; these samples cannot separate those costs.

A separate 7800X3D run has 41,346 transactions averaging 5.975 ms, with native
invocations averaging 2.485 + 3.174 ms. Its final compositor GPU timing is
20.558 ms. That GPU sample and the CPU lifetime averages cover different windows;
they must not be treated as an apples-to-apples CPU/GPU comparison. Neither report
justifies removing synchronization, eye isolation, auxiliary scope rendering, or
lowering resolution to claim a CPU optimization.

## Changes preserving display behavior

* Weapon HUD selection previously reread headers, quads and material names in
  two passes for each source. A bounded dispatch-local metadata snapshot now
  decodes them once, reusing results across both passes and weapon sources.
  The anchor validation, command ordering, narrative ownership exclusion, text
  reads, limits and malformed-stream rejection remain. No native or GPU calls
  occur between the selection passes; no data is reused across dispatches.
* The snapshot retains vector capacity per thread, grows only on demand, and is
  capped at 2,048 entries (28 bytes per entry on the tested compiler). It stores
  values, not borrowed pointers or COM resources. Inactive weapon capture does
  not populate it. This trades bounded metadata memory for repeated guarded
  native memory reads; it does not claim to reduce total resident memory.
* Indicator metadata uses local scratch until there is an actual warning/marker
  image to publish. Empty/menu dispatches no longer allocate and destroy a shared
  object. Actual publication allocation occurs outside the publication mutex.
  Empty results still clear stale output. Rendering and publication order stay
  unchanged.

## Validation

`tests/vr/generate_hud_selection_test.py` extracts the actual current selector
and compares it to the frozen pre-change implementation. Compile its generated
`build/language-tests/hud_selection_test.cpp` with C++20 and `src/client` on the
include path; assertions must be enabled.

The 1,200 deterministic streams cover single/dual sources, transparent anchors,
blur pairing, text and unrelated material selection, narrative exclusions,
malformed sizes, missing terminators and invalid coordinates. All acceptance
decisions and successful selections, ordering, bounds and blur pointers match
(1,437 accepted source selections). Guarded stream reads: 202,723 -> 87,195;
material lookups: 61,843 -> 21,737. These are synthetic operation counts, not FPS
or real-world frame-time gains. Snapshot limit/reset/failure recovery also pass.

No live game, saved game or installed mod was modified. The previously requested
local uninstall remains respected. Same-mission before/after CPU/GPU timing and
headset visual validation are still required; the old report had weapon capture
disabled, so it cannot establish the gain from the weapon-selector change.
