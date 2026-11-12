# Third-party components of the native renderer

The native renderer is licensed under the GNU GPL v3.0 (`LICENSE`). Code from other projects keeps
its own licence. No game files and no game shaders are in the source repository or in any release:
the shader cache is built on the player's own PC from their own game files.

| Component | How it is included | Licence | Where the text lives |
|---|---|---|---|
| [re:Blue](https://github.com/zolaware/reblue) renderer core (zolaware, Blue Dragon recomp), via the SvR 2010 recomp | adapted in `src/` | BSD 3-Clause | `native/LICENSE.reblue` and each file's header |
| [plume](https://github.com/zolaware/plume) (rendering hardware interface; copyright renderbag and contributors) | fetched into `third_party/plume`, plus patch | MIT | `third_party/plume/LICENSE` (after fetch) |
| [reblue-XenosRecomp](https://github.com/zolaware/reblue-XenosRecomp) (shader recompiler, build tool; copyright hedge-dev and contributors) | fetched into `third_party/XenosRecomp`, plus patch | MIT | `third_party/XenosRecomp/LICENSE.md` (after fetch) |
| [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler) 1.8.2407 (`dxcompiler.dll`, `dxil.dll`) | from XenosRecomp's `thirdparty/dxc-bin`; used at build time and shipped next to the exe | `dxcompiler.dll`: University of Illinois/NCSA; `dxil.dll`: Microsoft's licence terms for it | `third_party/licenses/DirectXShaderCompiler-LICENSE.txt` |
| [zstd](https://github.com/facebook/zstd) (single-file decoder) | `third_party/reblue_thirdparty/zstd` | BSD | `third_party/reblue_thirdparty/zstd/LICENSE` |
| [miniz](https://github.com/richgel999/miniz) (linked-shader cache) | `third_party/reblue_thirdparty/miniz` | public domain (Unlicense) | `third_party/reblue_thirdparty/miniz/LICENSE` |
| [xxHash](https://github.com/Cyan4973/xxHash) | via the ReXGlue SDK / re:Blue | BSD 2-Clause | `release/licenses/xxHash.txt` |
| [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (recompiler + runtime; portions from Xenia) | built separately | BSD 3-Clause | `release/licenses/ReXGlue-SDK-LICENSE.txt` |

The runtime libraries inside the ReXGlue SDK build (FFmpeg LGPL, SDL 3, Dear ImGui, fmt, spdlog,
toml++, and others) are listed in `release/CREDITS.md` and `release/licenses/`.

The base game recomp, [wowjinxy/RumbleRosesReX](https://github.com/wowjinxy/RumbleRosesReX), has no
licence file in its repository, so no licence terms for it are stated here.

The licence texts for the fetched/vendored third-party code (plume, XenosRecomp, zstd, miniz, DXC)
must be copied into the source repo's `third_party/` when it is published (the fetch script does
this for plume and XenosRecomp).
