# Movement compensation correction

The previous hand solver used camera-minus-placement from the last completed
stereo clone, with three separately published atomic coordinates and no scene
identity. Cloning occurs after native CPU producers complete, so skeleton work
could use an older correction. A changing correction leaves a residual shared by
the hands, arms and weapons.

The solver now computes `solve_origin - native_placement` at each live/empty-hand
or stored-weapon solve. The native placement source is the same block copied into
the scene record; completed stereo clones no longer publish a correction cache.
This also keeps the existing attached-equipment solve-origin rebasing consistent.
Only the rendered bone copies are translated. The unshifted solved gameplay pose,
tracking filters, recoil, input, ammunition, eye projections and resolution are
unchanged. No additional smoothing or deliberate tracking delay is introduced.

Native ABI evidence from `ref/h1_sp64_dump.exe` (outside the repository):

* R_SetSceneParms, entry 0x14058F8F0, writes refdef+0x58/5C/60 to
  0x151C277F0/F4/F8 at 0x14058F9EC..0x14058FA12.
* Its caller 0x14018691D initializes the placement block before scene/model work
  in CG_DrawActiveFrame. Native generator CPU jobs drain before returning.
* R_SetupViewInfo copies block 0x151C277E8 to record+0x188 at
  0x140591720..0x14059172D, so block+8 is record+0x190's current placement.

Exact producer/consumer instruction bytes are checked before installing the
hand hooks. A mismatch stops initialization with a specific error instead of
reading an unverified address. Ordinary 64-unit and mounted-view 2048-unit bounds
are retained, as are finite-input checks.

`placement_compensation_tests.cpp` checks 1,000 moving frames with variable
origin/placement separation and several target points, the same-scene cancellation
identity, zero/boundary values, mounted-view bounds and NaN/infinity. It passes;
the equivalent stale-correction model leaves 8,865 nonzero coordinate errors in
that synthetic sequence. These are algebra tests, not a headset reproduction.

With `MWR_NATIVE_PLACEMENT_TEST`, the standalone test also reserves a synthetic
native block in its own process and invokes the actual production reader. One
hundred consecutive native-position updates and the normal/mounted guard switch
pass. No game process or saved profile is involved in this integration test.

Real movement, acceleration, stopping, turning, stored equipment and mounted views
still require headset checks. The original report had only 21 world submissions;
it does not establish that this defect accounts for every perceived movement
artifact, including compositor reprojection or frame pacing.
