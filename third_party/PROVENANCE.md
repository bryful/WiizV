# Dependencies

Retrieved 2026-10-24. These are vendored dependencies; building the Windows frontend does not download anything.

## Snes9x libretro core

- Binary: https://buildbot.libretro.com/nightly/windows/x86_64/latest/snes9x_libretro.dll.zip
- Runtime reports: Snes9x 1.63 fae2fea
- DLL SHA256: D5E144475DEEB22D152D1E28A2649F5A53092A65C9EC204C6F5D97968F30F139
- Source matching reported upstream commit: https://github.com/snes9xgit/snes9x/tree/fae2fea08f74180759ef540ee94259213f503480
- Source archive: https://codeload.github.com/snes9xgit/snes9x/zip/fae2fea08f74180759ef540ee94259213f503480
- Source ZIP SHA256: 7F6DEE7AB1A367DA86BC86A87A204BF8FE0BE389D62E7CFBD568DC2CCAC32055
- Source archive included as snes9x/source.zip. The original unmodified binary is retained for compatibility checks. This records its identity; byte-for-byte reproducibility of that upstream binary has not been verified.
- LICENSE and component license notices are included beside the DLL in the dependency folder and copied into the application's licenses folder.
- Non-commercial restrictions apply; consult the original notices.

## WiizV source-built Snes9x core

- The default core is `wiizv_snes9x.dll`, built with MSVC from the upstream commit listed above.
- `tools/prepare-core.ps1` extracts the verified upstream source archive into `third_party/snes9x/upstream` and applies the APU-clock conversion, movement, and persistent-effect hook changes. It also generates `WiizVCore.vcxproj` and the export definition file.
- `third_party/snes9x/wiizv_audio.inc` adds the exported `wiizv_set_audio_speed` function. It scales SMP clocks by the reciprocal of the frontend's selected game speed. DSP sample rate and audio waveform synthesis are unchanged.
- `third_party/snes9x/wiizv_movement.inc` exposes exploration-state checks and a movement request. It redirects execution at a verified game instruction boundary to the game's existing floor-loading movement routine. The frontend enables this only for the verified user-supplied ROM hash. No game data is embedded in this extension.
- `third_party/snes9x/wiizv_effects.inc` adds `wiizv_set_effects` and reapplies enabled party spell effects at verified game instruction boundaries. Turning an effect off clears its runtime value and stored character duration. It also maintains the current MP nibble at 9 without changing maximum MP or learned spells; `wiizv_normalize_mp` restores current counts to their normal limits when disabled or loading a state with the option off. The frontend enables this only for the verified ROM hash.
- `third_party/snes9x/wiizv_linkage.h` fixes C linkage for Settings, OpenBus, and LineMatrixData in the mixed C/C++ MSVC build. The build defines `__inline__=__inline` for upstream C code and uses UTF-8 source input.
- The source archive, extension includes, linkage header, and preparation/build scripts are copied into the application's `licenses/snes9x` folder. The upstream license applies to the modified core.
- To reconstruct the modified sources, use the repository layout: original archive, the `.inc` extension files and linkage header in `third_party/snes9x`, scripts in `tools`. Run `tools/build-core.ps1`. Visual Studio 2026 with the C++ desktop workload is required.
- WiizV states use the core DLL's SHA256 as their compatibility identifier. The original buildbot hash above and previous WiizV cores (`9f42fb5ef81ad89adb57a47643f9f62badde4ca0c3c8a6709b98649cbf45cd7e`, `92869c40012d003d8c5194824e14b3270db9f28235ce769064d6f967304a4309`, `854326931f62dcbfee7774e51973921618c28223653b298e4ffe743eabc479b6`, `3d972590a2fdfb97c90139252eae31068ede41aa0c57127c163631ae48b4f0a4`, `d668312162011c27f98dda326a3bceeaceec140c979518dfcf56ceb457da04be`) are additionally accepted after verifying restored video/WRAM outcomes. These changes do not alter the serialization layout. No other arbitrary DLL is treated as compatible.

## libretro API header

- https://github.com/libretro/libretro-common/blob/master/include/libretro.h
- SHA256: 036DDFFC995AD116C3081CA0A7515D09C2A116F3483E4F5012FE0C29A885DA76
- MIT notice is embedded in the header.

## SDL3

- Version: 3.4.4, Windows VC development SDK, copied from the existing local SDK in GameTest/_lib/SDL3-3.4.4.
- Upstream: https://github.com/libsdl-org/SDL/releases/tag/release-3.4.4
- SDK: https://github.com/libsdl-org/SDL/releases/download/release-3.4.4/SDL3-devel-3.4.4-VC.zip
- License: SDL3/LICENSE.txt (zlib).

ROMs and the game manual are local user-supplied files and are not included in build output.
