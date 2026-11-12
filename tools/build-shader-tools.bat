@echo off
rem RR-NR-12: build the native renderer's shader tools from native\third_party (rr_fetch_third_party.py first):
rem   XenosRecomp.exe (UFC stack + xenosrecomp-0e.patch) and rrxx_shader_cache.exe (native\tools\native\shader_extract),
rem then gather them with dxcompiler.dll + dxil.dll (XenosRecomp's dxc-bin = the DXC the game build links) in
rem   rumble-roses-xx-recomp-v010\out\native-tools\
rem (the launcher package takes them from there: release\build\package.ps1 -ShaderTools <that folder>).
rem Same recipe as research\rr-nr3\build_xenos.bat + build_tools.bat (RR-NR-3).
setlocal
set "ROOT=%~dp0..\.."
set "TP=%~dp0..\third_party"
set "B=%ROOT%\rumble-roses-xx-recomp-v010\out\native-tools-build"
set "OUT=%ROOT%\rumble-roses-xx-recomp-v010\out\native-tools"
call "%ROOT%\env.bat"
python -I "%~dp0rr_fetch_third_party.py" --check || exit /b 1
cmake -S "%TP%\XenosRecomp" -B "%B%\xenosrecomp" -G Ninja -DCMAKE_BUILD_TYPE= -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_RC_COMPILER=llvm-rc "-DCMAKE_C_FLAGS=-O2" "-DCMAKE_CXX_FLAGS=-O2 -g -DREBLUE_RECOMP" || exit /b 1
cmake --build "%B%\xenosrecomp" --target XenosRecomp || exit /b 2
cmake -S "%~dp0native\shader_extract" -B "%B%\shader_extract" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=clang++ "-DRRXX_SC_THIRD_PARTY=%TP%" || exit /b 3
cmake --build "%B%\shader_extract" || exit /b 4
if not exist "%OUT%" mkdir "%OUT%"
copy /y "%B%\xenosrecomp\XenosRecomp\XenosRecomp.exe" "%OUT%\" >nul || exit /b 5
copy /y "%B%\shader_extract\rrxx_shader_cache.exe" "%OUT%\" >nul || exit /b 5
copy /y "%TP%\XenosRecomp\thirdparty\dxc-bin\bin\x64\dxcompiler.dll" "%OUT%\" >nul || exit /b 5
copy /y "%TP%\XenosRecomp\thirdparty\dxc-bin\bin\x64\dxil.dll" "%OUT%\" >nul || exit /b 5
echo Shader tools: %OUT%
