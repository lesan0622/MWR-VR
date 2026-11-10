# Onboarding, handedness and MWR weapon actions

The Visual Comfort onboarding page still submitted `r_mbLimit`, which had been
removed from the settings catalog. Its missing draft value rejected Next with
`error.invalidVR`. The six-page guide now uses a shared, tested field list without
that stale entry. Page 2 adds independent control-hand and pistol-holster choices.

`vr_controlHand` swaps movement/turn sticks, stick clicks and primary/menu roles,
sets the initial equipment and menu-pointer hand, and routes the HUD/vehicle
buttons. Physical tracking, triggers, grips, releases and haptics retain their
left/right indices. `vr_pistolHolsterSide` controls initial pistol placement;
explicit stows and restored inventory topology retain their actual locations.
Both choices take effect at process startup. Native current/latched values are
seeded from the launcher selection. The config parser now also recognizes native
hashed enum names so a subsequent game save cannot lose these choices.

The existing per-instance chamber ledger already stores one actual cartridge
separately from magazine rounds. Native total N imports as (N-1)+1. Tests cover
magazine removal, a single chamber-only shot, a rejected second shot, cycling
without a magazine, feeding one cartridge from a magazine and tactical reloads.
No additional cartridge is granted. Open-bolt feeds keep their own semantics.

Native empty-reload captures measured MP5 travel from x=12.410398 to 8.461279
inches (100.330 mm, with the native slight downward axis), and G3 from x=15.369194
to 10.639433 (120.136 mm). Both previously inherited the MP5K's 65 mm stroke.
The native endpoints now determine travel; raised handle orientation is rebased
from the source rest orientation into the MWR rest frame.

MP5/MP5 wet and M4 use reviewed disconnected bolt faces, guarded by exact native
surface counts and moving-piece bounds before replacing a rigid source group.
M16 uses its native `j_bolt` bone, separate from `j_clip_release`. Manual-only
linkages do not synthesize a firing cycle; native M16 firing animation remains
in charge during shots. Original asset meshes are not edited.

Silenced MP5 WeaponDefs use `weap_mp5sd_*` reload keys even with a dry receiver.
The actual admitted silenced weapon now selects those keys, preserving native
lookup, alias randomization and sound-part selection.

Validation: the focused C++ suite covers 136 closed-bolt hand/profile cases,
independent handedness/holster combinations, native handle endpoints, four MWR
bolt linkages and MP5SD sound selection. Config tests cover independent choices
and hashed enum round trips. The onboarding regression and launcher production
build pass. The full UI suite still has two pre-existing Chinese translation
failures (missing wrist-ammo labels and a maintenance-notice suffix).

Native captures used a protected mock runtime; all 12 player files were restored
and SHA-256 verified afterward. Actual headset comfort/interaction remains to
be checked. This change is for local installation only; no public release.
