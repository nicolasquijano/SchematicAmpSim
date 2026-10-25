@echo off
rem Builds SchematicAmpSim with the Visual Studio Build Tools already on this machine.
rem   build.cmd                 configure + build everything
rem   build.cmd test            ... and run the tests
rem   build.cmd target <name>   build one target (e.g. SchematicAmpSim_VST3, sas_tests)
setlocal
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cd /d "%~dp0"

cmake --preset release || exit /b 1
if /i "%~1"=="target" (
  cmake --build --preset release --target %~2 || exit /b 1
) else (
  cmake --build --preset release || exit /b 1
)
if /i "%~1"=="test" ctest --preset release || exit /b 1
endlocal
