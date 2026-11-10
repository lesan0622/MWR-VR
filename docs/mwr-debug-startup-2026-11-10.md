# Launcher debug startup settings

Reported: launcher Debug selections have no effect after fully restarting.

Confirmed parser defect: native hashed boolean assignments (for example the
hash of vr_debugViewProbes) were recognized only for multi-level toggles. Ordinary
debug booleans silently remained false. The focused regression fails on the old
parser with `Hashed debug setting ignored: vr_debugViewProbes`.

All whitelisted toggles now accept their native hash alias as well as the plain
name. Saving removes either representation before writing the canonical name,
so a stale hashed zero cannot silently override the user's new selection.
Malformed/non-boolean levels remain rejected. Unrelated config lines survive.

Native registration now seeds current/latched debug dvar values from the same
frozen startup selection that controls diagnostic hooks, while keeping reset
defaults false. This closes a discrepancy where registration began at false
and relied entirely on native profile load ordering. Enabled startup selections
are explicitly logged as `[VR debug] <name> enabled at startup`.

`tests/vr/debug_startup_tests.cpp` verifies all eleven hash aliases, canonical
merge/removal of old aliases, malformed values, and frozen enabled startup state.
It passes after the fix. Live rendering of every diagnostic overlay is not
established by this configuration regression; recordings and geometry views also
require the relevant scene/weapon. No forced enablement of user debug settings
or changes to saves/config files are part of deployment.
