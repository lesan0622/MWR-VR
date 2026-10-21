# MWR tutorial input

The MWR controller adapter now connects the existing physical `ads_policy` to
the final usercmd, after native FinishMove. Holding a bullet weapon with both
hands and raising it into the sight corridor requests native ADS (bit 0x800).
The original 100 ms dwell, support-grip requirement, tracking freshness and
native weapon restrictions apply. `vr_autoAds` defaults to enabled.

Native `notifyoncommand` listeners need a reliable binding notification in
addition to usercmd button bits and `weapon_fired`. Each deliberate trigger
press from a held weapon's firing hand now sends the native `+attack`
notification. ADS transitions send `+speed_throw` / `-speed_throw`. These
notifications do not fire extra bullets or set tutorial completion flags.

MWR command instructions at 0x1401AA0B9 / 0x1401AA0C2, the existing native
notification function and the binding names are checked before use.
`vr_input_status` reports ADS requests, command counts and notifications.

On 2026-10-18 the installed `overlord.exe` still lacked `vr_autoAds`, the ADS
adapter diagnostic, and the attack-notification diagnostic. The workspace
executable contained all three. The tutorial input changes had therefore
not reached the installed executable.

Validation on 2026-10-18: the controller-input suite passed, including
physical ADS and button continuity regressions. The production
`fire_owned` adapter test now uses MWR's direct `Scr_AddString` / native
notify endpoints and passed the notification-before-bullet, exact weapon,
same-tick instances, rejected shots, reentry, and lifecycle cases.
Only the native endpoints and loaded `scr_const` address are substituted.
`tests/vr/mwr_tutorial_input_tests.py` additionally executes the production
controller notification/ADS blocks, covering either firing hand, one event
per press, stale input, raised/lowered ADS transitions, and preservation of
native buttons. These checks passed.
This confirms the existing bullet transaction without synthesizing hits or
changing tutorial flags. The live G36C / firing station 1 test remains
unverified; offline tests do not establish checkpoint progression.

The staged installer is `D:\overlordmwr\port\tutorial-target-fix\install.cmd`.
It checks the patch and original game hashes, requires the game to be closed,
backs up the existing mod EXE/PDB, and verifies the installed files. Player
saves and settings are not part of the package.

The Release build succeeded using the x64-host v145 compiler with four
compiler workers. The staged EXE/PDB and game-version checks passed.
Automatic installation was denied write access to the Steam game folder.
The installed EXE/PDB still match their original hashes and the verified
backup; the user must run the staged installer in their own Windows session.
