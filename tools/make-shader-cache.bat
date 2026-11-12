@echo off
rem RR-NR-12: build shader_cache.bin + shader_cache_linked.bin for a dev / farm build of the v010 tree, the
rem way the launcher does it on a player's machine (release\launcher ShaderCache.cs):
rem   rrxx_shader_cache.exe <game data> <cache dir>, with XenosRecomp.exe + rrxx_prelink.exe + DXC beside it.
rem   make-shader-cache.bat [build dir] [game data dir]
rem Defaults: rumble-roses-xx-recomp-v010\out\build\win-amd64-relwithdebinfo, rumble-roses-xx-recomp-v010\assets.
rem Needs build-game-v010-native.bat (rrxx_prelink.exe, dxcompiler.dll beside the exe) and build-shader-tools.bat.
rem The tools are copied beside the exe (as in a player's game\ folder). The cache is derived from the game's
rem files: never commit or ship it (package.ps1 refuses shader_cache*).
setlocal
set "ROOT=%~dp0..\.."
set "BUILD=%~1"
if "%BUILD%"=="" set "BUILD=%ROOT%\rumble-roses-xx-recomp-v010\out\build\win-amd64-relwithdebinfo"
set "DATA=%~2"
if "%DATA%"=="" set "DATA=%ROOT%\rumble-roses-xx-recomp-v010\assets"
set "TOOLS=%ROOT%\rumble-roses-xx-recomp-v010\out\native-tools"
if not exist "%BUILD%\rrxx_prelink.exe" ( echo %BUILD%\rrxx_prelink.exe missing: build-game-v010-native.bat first & exit /b 1 )
if not exist "%TOOLS%\rrxx_shader_cache.exe" ( echo %TOOLS% missing: native\tools\build-shader-tools.bat first & exit /b 1 )
for %%f in (rrxx_shader_cache.exe XenosRecomp.exe) do copy /y "%TOOLS%\%%f" "%BUILD%\" >nul || exit /b 1
"%BUILD%\rrxx_shader_cache.exe" "%DATA%" "%BUILD%" || exit /b 2
echo Shader cache: %BUILD%\shader_cache.bin
