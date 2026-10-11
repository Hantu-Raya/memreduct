@echo off
rem Build Release x64 with a portable MSVC toolchain (no Visual Studio install).
rem Usage: build_portable.bat [path\to\msvc]   (default: ..\tools\msvc)
setlocal enableextensions
cd /d "%~dp0"

set "MSVC_ROOT=%~1"
if "%MSVC_ROOT%"=="" set "MSVC_ROOT=%~dp0..\tools\msvc"
if not exist "%MSVC_ROOT%\setup_x64.bat" (
	echo Portable MSVC not found at "%MSVC_ROOT%".
	exit /b 1
)
call "%MSVC_ROOT%\setup_x64.bat"

set "OUT=bin\64"
set "OBJ=x64\Release\memreduct"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"

set DEFS=/DMICROSOFT_WINDOWS_WINBASE_H_DEFINE_INTERLOCKED_CPLUSPLUS_OVERLOADS /D_UNICODE /DUNICODE /DWIN32_LEAN_AND_MEAN /DAPP_HAVE_AUTORUN /DAPP_HAVE_SKIPUAC /DAPP_HAVE_TRAY /DAPP_HAVE_UPDATES /DNDEBUG
set CFLAGS=/nologo /c /W3 /WX /O2 /Oi /Oy /Ob2 /GF /Gy /Gz /MT /Zi /sdl /guard:cf /guard:ehcont /std:clatest /Zc:preprocessor /QIntel-jcc-erratum /I..\routine\src /Isrc\include /Isrc /Fo%OBJ%\ /Fd%OBJ%\vc.pdb

cl %CFLAGS% %DEFS% src\main.c ..\routine\src\routine.c ..\routine\src\rapp.c || exit /b 1
rc /nologo /d _UNICODE /d UNICODE /d _WIN64 /i ..\routine\src /i src /fo %OBJ%\resource.res src\resource.rc || exit /b 1
link /nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /DEBUG /RELEASE /CETCOMPAT /guard:cf /guard:ehcont /DEPENDENTLOADFLAG:0x800 /BREPRO /OUT:%OUT%\memreduct.exe /PDB:%OUT%\memreduct.pdb %OBJ%\main.obj %OBJ%\routine.obj %OBJ%\rapp.obj %OBJ%\resource.res kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib || exit /b 1
echo built %OUT%\memreduct.exe
