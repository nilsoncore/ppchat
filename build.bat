REM Windows build script file
REM Usage:
REM     "build.bat" or "build.bat all" - Execute both Debug and Release builds;
REM     "build.bat debug" - Execute Debug build;
REM     "build.bat release" - Execute Release build.

@echo OFF

call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
echo.

set build_debug=0
set build_release=0

IF "%~1"=="" (
	set build_debug=1
	set build_release=1
	echo Build option argument is not provided, executing both Debug and Release builds.
) ELSE IF "%~1"=="all" (
	set build_debug=1
	set build_release=1
	echo Build option argument is set to "%~1", executing both Debug and Release builds.
) ELSE IF "%~1"=="release" (
	set build_release=1
	echo Build option argument is set to "%~1", executing Release build.
) ELSE IF "%~1"=="debug" (
	set build_debug=1
	echo Build option argument is set to "%~1", executing Debug build.
)

IF %build_debug%==1 (

REM Build shared dll, client, and server in Debug mode.

echo.
echo --- Debug build ---
echo.

echo [ppchat-shared] Building debug shared dll:
devenv "ppchat.sln" /build Debug /project ppchat-shared /nologo
echo.

echo [ppchat-server] Building debug server:
devenv "ppchat.sln" /build Debug /project ppchat-server /nologo
echo.

echo [ppchat-client] Building debug client:
devenv "ppchat.sln" /build Debug /project ppchat-client /nologo
echo.
)

IF %build_release%==1 (

REM Build shared dll, client, and server in Release mode.

echo.
echo --- Release build ---
echo.

echo [ppchat-shared] Building release shared dll:
devenv "ppchat.sln" /build Release /project ppchat-shared /nologo
echo.

echo [ppchat-server] Building release server:
devenv "ppchat.sln" /build Release /project ppchat-server /nologo
echo.

echo [ppchat-client] Building release client:
devenv "ppchat.sln" /build Release /project ppchat-client /nologo
echo.
)

echo.
echo All builds completed.
echo.