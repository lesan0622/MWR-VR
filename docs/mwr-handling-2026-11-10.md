# Handling adjustment, 2026-11-10

Charging-handle acquisition gets 2 cm of extra reach. G36 explicitly retains
its original reach; reciprocating pistol slides and rotating bolts retain theirs.
Both central interaction arbitration and the gesture controller use the same
radius. Stroke length, motion constraints, breakaway and bolt-release/slap
volumes keep their existing meanings.

Attached magazine acquisition gets 2 cm of extra reach for direct-grasp feeds.
Profiles using the separate short-pistol support catch retain that mechanism.
The change concerns grabbing the magazine, not insertion alignment or latch
activation. Manual latches and rock-in feeds still require their original removal
motions. Existing button-magazine pre-grasp behavior is retained: holding or
pulling the seated magazine does not detach it; pressing the rear release
transfers the real magazine into the held hand. Ammunition is not synthesized.

`tests/vr/handling_reach_tests.cpp` runs the existing button-magazine and magazine
feedback regressions plus 38 left/right button pre-grasp scenarios at the expanded
boundary. It checks ammunition conservation, no removal before release, transfer
after release, M4 handle acquisition inside/outside the new boundary and the G36
exception. The focused executable passes with zero failures.

The full historical physical-reload suite is not passing on this MWR port: its
native storage fixtures contain H2 offsets beyond the current H1 storage extent
(compiler C4789 warnings and storage assertion failures). Those unrelated legacy
fixtures are not invoked by the focused executable, and were not repaired as part
of this handling adjustment. This does not replace headset interaction testing.

The user also provided an early console log ending at `main_lockout`. It has no
graphic failure state or description. The thermal-world warning is also present
in earlier working runs. No speculative graphics fix was made from that warning;
the visible symptom and a failure-time diagnostic bundle are still needed.
