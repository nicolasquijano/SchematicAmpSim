@echo off
rem Packages a release: build, test, zip and (with Inno Setup 6) the installer, all into dist\. See docs\RELEASING.md.
rem   package.cmd              build + tests + packages
rem   package.cmd -SkipTests   without the test run
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1" %*
exit /b %ERRORLEVEL%
