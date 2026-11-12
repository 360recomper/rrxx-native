# rrxx-native

Native renderer for the Rumble Roses XX recomp (v1.5c and later). Licensed GPL-3 (see LICENSE; reblue-derived parts:
LICENSE.reblue; third-party notices: THIRD-PARTY.md).

The released game zip (https://github.com/360recomper/RumbleRosesReX/releases) ships this renderer as the default, with
the original emulated renderer as an automatic fallback. This repo holds the source that matches the v1.5c release build.

## What is here
- `src/` - the renderer (D3D12/Vulkan through plume, shader translation through XenosRecomp, game hooks).
- `patches/` - our patches to the two fetched third-party libraries (plume, XenosRecomp).
- `third_party/` - small vendored pieces and licenses. plume and XenosRecomp are not committed:
  `python tools/rr_fetch_third_party.py` clones them at their pinned commits and applies `patches/`.
- `tools/` - fetch script and the shader-cache tools.
- `patches-game-sdk/` - the full changes to the game recomp (wowjinxy/RumbleRosesReX) and to rexglue-sdk v0.10 that
  the v1.5c build uses, including the hooks that turn this renderer on.

## Building (outline)
1. Clone rexglue-sdk (v0.10) and the game recomp; apply the two patches in `patches-game-sdk/`.
2. Place this repo as `native/` next to the game folder and run `python tools/rr_fetch_third_party.py`.
3. Configure the game with `-DRRXX_NATIVE_RENDERER=ON -DRRXX_NATIVE_HOOKS=ON` and build Release.

No game data is included or needed to build; you need your own copy of the game to play.

Credits: YucaGuapo (RRXX port). Built on nefariousjosiah's SmackDown vs. Raw 2010 recomp renderer (GPL-3), re:Blue
(BSD-3), plume (MIT) and XenosRecomp (MIT); see THIRD-PARTY.md and UPSTREAM.txt.
