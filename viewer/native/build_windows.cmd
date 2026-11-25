@echo off
setlocal
where cl >nul 2>&1
if errorlevel 1 call :load_msvc
where cl >nul 2>&1
if errorlevel 1 (
    echo Visual Studio x64 C build tools and the Windows SDK are required.
    exit /b 1
)
set "target=%~1"
if not defined target set "target=%~dp0..\..\dist\AirConflicts_Viewer_Windows_x64.exe"
for %%I in ("%target%") do (
    if not exist "%%~dpI" mkdir "%%~dpI"
    set "target=%%~fI"
)
set "work=%TEMP%\acpc-viewer-%RANDOM%-%RANDOM%"
mkdir "%work%" || exit /b 1
pushd "%work%" || exit /b 1
cl /nologo /std:c11 /utf-8 /O2 /W4 /MT /Fe:"%target%" "%~dp0model.c" "%~dp0texture.c" "%~dp0render.c" "%~dp0windows.c" /link /SUBSYSTEM:WINDOWS /MACHINE:X64 /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA user32.lib gdi32.lib comdlg32.lib comctl32.lib shell32.lib gdiplus.lib
set "status=%ERRORLEVEL%"
popd
rmdir /s /q "%work%"
exit /b %status%

:load_msvc
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" exit /b 1
for /f "usebackq delims=" %%I in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%I\VC\Auxiliary\Build\vcvars64.bat"
exit /b
