# MWR VR

English | [한국어](README.ko.md)

![MWR VR](src/launcher-ui/public/images/launcher-background.jpg)

MWR VR is an unofficial VR mod for the campaign of *Call of Duty: Modern Warfare Remastered* (2016). It is a port of [Overlord](https://github.com/Aeka0/Overlord), the VR mod for *Modern Warfare 2 Campaign Remastered*: stereoscopic rendering, 6DOF head and hand tracking, physical weapon handling, and VR interactions for equipment and story sequences, rebound to the MWR executable and adapted to its missions. Wherever possible it reuses the game's own weapon, ammunition, animation and mission logic, so the original story presentation is kept.

### **This is Beta 9, maintained by one person. Many features have been tested only on the maintainer's own machine, and stability is expected to be poor. Please treat it as a test build, not a finished product.**

### **Mods made for the non-VR game are not expected to work with it.**

## Main Features

- **Stereo view and head tracking** for the whole campaign, with the native menus and HUD presented in VR.
- **Two-handed weapons**: hold and aim with the controllers, off-hand grips, body holsters, picking weapons up from the world.
- **Physical reloading and chambering** per weapon design: magazines, bolts, slides and charging handles. AK rifles and the Dragunov use the AK rock-in magazine (hook the front, rock it back).
- **Underbarrel grenade launchers**: M4 M203 (slide the barrel forward, load at the breech, close) and AK GP25 (muzzle-loaded), with rounds drawn from the waist.
- **Mission equipment**: Javelin and Stinger lock-on displays, C4 detonator, night vision with weapon lasers that follow the VR gun, throwables and knife.
- **Mission adaptations** for MWR:
  - *One Shot, One Kill*: the Barrett rests on its bipod and is aimed with the hands inside the turret arcs.
  - *Death From Above*: the AC-130 screen is aimed with the right stick; weapon cycling and thermal on controller buttons.
  - Mounted and vehicle guns (e.g. the Mk19 in *Shock and Awe*): the hands hold the handles and the arms swing the gun.
  - *Safehouse*: Cobra air support — raise a hand to your ear and press Grip, point at the target, press Trigger.
  - *Aftermath*: crawl out of the wreck by planting your hands and pulling.
  - Ladders climbed hand over hand.
  - Claymores planted by reaching to the floor; dogs fought off with Grip/Trigger at the prompt.
- **Real crouching and lying down** select the native stance (or use the stick).
- **Javelin/Stinger head aim** option: aim and lock with the head instead of the tube.
- **Desktop launcher** with first-time setup, VR settings, comfort and assistance options, and control help.

## Requirements

- 64-bit Windows 10 or 11.
- A legally owned and installed copy of *Call of Duty: Modern Warfare Remastered* on **Steam**. The Battle.net copy is detected by the installer, but the mod has not been tested with its executable.
- SteamVR, and a headset with tracked controllers that works with SteamVR.
- The .NET Framework 4.5 or later for the installer (included with Windows 10/11).

The client uses Direct3D 11 and OpenXR by default (OpenVR remains selectable in the launcher). Default controller bindings come from Overlord: Oculus Touch (Meta Quest), Valve Index and Vive controllers. Meta Quest controllers through SteamVR are the primary target; other devices may need binding or alignment changes.

Pirated or cracked copies of the game are not supported. Releases do not contain game files.

## Installation

Download the latest release from the [Releases page](https://github.com/lesan0622/MWR-VR/releases).

**Installer (recommended):** run `MWR-VR-Setup.exe`. It finds the game through Steam (and Battle.net), asks before installing, refuses while the game is running, writes only the mod's files and creates a desktop shortcut. If the game is not found, pick the game folder (the one containing `h1_sp64_ship.exe`).

**Manual:** extract `MWR-VR-<version>.zip` into the game folder:

~~~text
Call of Duty Modern Warfare Remastered/
├─ h1_sp64_ship.exe      (the game)
├─ overlord.exe
├─ openxr_loader.dll
├─ overlord.vrmanifest
├─ steamvr/
├─ vr_input/
└─ overlord-mwr/
~~~

Then start SteamVR, connect the headset and controllers, run `overlord.exe` (or the desktop shortcut) and choose Singleplayer.

## Basic Controls

| Action | Method |
| --- | --- |
| Move / turn | Left stick / right stick (smooth or snap turning in settings). |
| Hold a weapon | Put your hand on its grip and hold Grip. |
| Fire | Squeeze Trigger with the hand on the firing grip. |
| Reload | With the free hand, Trigger at the waist draws a magazine; insert it, then work the bolt or charging handle if the chamber is empty. |
| Grenade launcher | M203: slide the barrel forward, Trigger at the waist for a grenade, load it at the breech, close the barrel. GP25: Trigger at the waist, load it at the muzzle. Grip + Trigger at the waist picks the other ammunition. |
| Holster | Move the weapon to a body holster and release Grip. |
| Mounted gun | Right Grip mounts/dismounts; the arms swing the gun; Trigger fires. |

The launcher's Help page is inherited from Overlord and does not yet describe every MWR weapon.

## Known Issues

- Headset confirmation is still pending for several features (Barrett bipod, mounted-gun grips).
- A left-right flipped view has been reported in a cutscene; it has not been reproduced yet.
- From Overlord: a `Create2DTexture` error may be avoided by disabling Shader Preloading in the graphics settings; MSI Afterburner may prevent startup.

## Reporting Problems

Email **lesan00622@gmail.com** (or use the launcher's *Report by Email* button).

While the problem is visible, press **Ctrl+Shift+F8** with the game window selected, or run **`vr_diagnose`** in the console. After about five seconds Explorer opens with a diagnostic ZIP selected; attach it and describe what happened and where (mission, checkpoint, weapon). After a crash, attach the crash ZIP from the `minidumps` folder. Do not send game files or credentials.

## Building from Source

Clone with submodules (GitHub's source ZIP does not include them). Install Visual Studio with *Desktop development with C++* and the Windows SDK, Python 3 and Node.js (the launcher UI is built during the client build), then in a Developer Command Prompt:

~~~bat
git clone --recurse-submodules https://github.com/lesan0622/MWR-VR.git
cd MWR-VR
generate.bat
msbuild build\overlord.sln /m /p:Configuration=Release /p:Platform=x64
python tools\build_installer.py Release
~~~

Output goes to `build/bin/x64/Release/`; the installer and the manual-install ZIP go to `build/dist/`. See the [development guide](docs/development.md) and the [documentation index](docs/README.md) (inherited from Overlord, mostly about the shared VR systems).

## License and Acknowledgments

The code is licensed under [GNU GPLv3](LICENSE). Third-party code, fonts and other resources keep their own licenses; see the [third-party notices](THIRD_PARTY_NOTICES.md) and [source provenance](docs/source-provenance.md).

This port is built on [Overlord](https://github.com/Aeka0/Overlord) by [Aeka0](https://github.com/Aeka0), and on the client foundations of H2-Mod, h1-mod (MWR addresses and structures), [IW6x](https://git.alterware.dev/alterware/iw6-mod) and [S1x](https://git.alterware.dev/alterware/s1-mod). Thanks also to [momo5502](https://github.com/momo5502), [xensik](https://github.com/xensik/gsc-tool), [JariKCoding](https://github.com/JariKCoding/CoDLuaDecompiler), [ZoneTool](https://github.com/ZoneTool/zonetool), [quaK](https://github.com/Joelrau), and the related work of Valve, Khronos and REFramework.

This port is maintained separately and is not affiliated with or endorsed by Overlord's author; please send problems with this port here, not to the Overlord project. It is an independent community mod, not an official game release. Game names and trademarks belong to their respective owners. It must not be used for piracy or to circumvent copy protection.


