@ECHO OFF

IF NOT EXIST %0\..\..\dist (
    CALL mkdir %0\..\..\dist
)

ECHO Finding version
SET VERSION=0.0.0
FOR /f "tokens=1,2" %%a IN ('type %0\..\..\CMakeLists.txt') DO (
    IF "x%%a"=="xVERSION" (
        IF %VERSION% == 0.0.0 (
            SET VERSION=%%b
        )
    )
)
IF %VERSION% == 0.0.0 (
    ECHO Failed to find Version
    EXIT /B
)

ECHO Building version %VERSION%

IF EXIST %0\..\..\build (
    ECHO Removing old build folder
    RMDIR /S /Q %0\..\..\build
)
ECHO Creating new build folder
CALL mkdir %0\..\..\build

@REM Be sure to set your favourite compiler as en environment variable. `CC` for C, `CXX` for C++. Cmake will default to this
@REM https://cmake.org/cmake/help/book/mastering-cmake/chapter/Getting%20Started.html#specifying-the-compiler-to-cmake
ECHO Configuring CMake
CALL cmake --no-warn-unused-cli^
           -DCMAKE_BUILD_TYPE:STRING=Release^
           -S%0\..\..\^
           -B%0\..\..\build^
           -G Ninja

IF %ERRORLEVEL% NEQ 0 (
    ECHO CMake failed to configure with exit code %ERRORLEVEL%
    EXIT /B
)

ECHO Building release binaries
CALL cmake --build %0\..\..\build --config Release --target all --

IF %ERRORLEVEL% NEQ 0 (
    ECHO Release build failed with exit code %ERRORLEVEL%
    EXIT /B
)

@REM When we have tests, they should run here...
@REM ECHO Running tests...
@REM CALL %0\..\..\build\Release\tests.exe
@REM IF %ERRORLEVEL% NEQ 0 (
@REM     ECHO Tests failed with exit code %ERRORLEVEL%
@REM     EXIT /B
@REM )

ECHO Backing up .pdb files
IF NOT EXIST %0\..\..\build\Release\Scream_plugin.pdb (
    ECHO Missing Scream_plugin.pdb
    EXIT /B
)
@REM Windows will prompt to ask you whether the destination is a directory or file
@REM Echo is used to pipe the answer to the prompt
ECHO F | XCOPY %0\..\..\build\Release\Scream_plugin.pdb %0\..\..\dist\Scream_v%VERSION%_plugin.pdb /Y

@REM To my knowledge, call strip here does nothing when building with clang.
@REM Maybe older versions of clang on windows needed symbol stripping,
@REM but Clang 16 seems to follow MSVC conventions of keeping all the symbols in a .pdb file
CALL llvm-strip -x %0\..\..\build\Release\Scream_plugin.vst3

ECHO Building installer
CALL ISCC.exe /DMyVersion=%VERSION% %0\..\_windows.iss

IF %ERRORLEVEL% NEQ 0 (
    ECHO Inno Setup failed to build installer with exit code %ERRORLEVEL%
    EXIT /B
)