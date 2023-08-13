@echo OFF

REM Windows MSBuild build script file
REM Usage:
REM "build" or "build all" - Execute both Debug and Release builds;
REM "build debug" - Execute Debug build;
REM "build release" - Execute Release build.

REM Important note:
REM This build script assumes that the path to the msbuild.exe file is provided.
REM However, this is not the case by default.  In order to add it to
REM default path list (PATH system variable on Windows), you need to open:
REM "Settings -> System -> Advanced system settings -> Environment Variables".
REM There would be User variables and System variables.  It is recommended
REM to addthe path to User variables.  Click "Edit" button under User variables
REM section, and then add the path to the folder containing msbuild.exe file.
REM For example, for Visual Studio 2019 Community the path is:
REM "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\".

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

msbuild "ppchat.sln" -nologo -nowarn:MSB8028 -property:Configuration=Debug -property:Platform=x64 -t:ppchat-shared:rebuild -t:ppchat-server:rebuild -t:ppchat-client:rebuild
echo.

)

IF %build_release%==1 (

REM Build shared dll, client, and server in Release mode.

echo.
echo --- Release build ---
echo.

msbuild "ppchat.sln" -nologo -nowarn:MSB8028 -property:Configuration=Release -property:Platform=x64 -t:ppchat-shared:rebuild -t:ppchat-server:rebuild -t:ppchat-client:rebuild
echo.

)

echo All builds completed.
echo.