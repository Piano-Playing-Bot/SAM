@echo off

:: -mwindows : Compile a Windows executable, no cmd window
set PROD_FLAGS=-O2 -mwindows -s
set DEV_FLAGS=-g -ggdb
set CFLAGS=-Wall -Wextra -Wimplicit -Wpedantic -Wno-unused-function -std=c99

:: Compiler-Options to include dependencies
set LIB_PATHS=-L./bin
set INCLUDES=-I./deps/stb -I./deps/tsoding -I./deps/QuelSolaar -I./deps/raylib/src -I./deps/raygui/src
set RAYLIB_DEP=-lraylib -lopengl32 -lgdi32 -lwinmm
set DEPS=%INCLUDES% %LIB_PATHS% %RAYLIB_DEP%

:: Build all dependencies
set BUILD_ALL=
if "%~1"=="a" set BUILD_ALL=1
if not exist bin set BUILD_ALL=1
if defined BUILD_ALL (
	:: Remove old bin folder
	rmdir bin /S /Q
	mkdir bin
	xcopy assets\ bin\assets\ /E /Q
	:: Build raylib
	cd deps/raylib/src
	make PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=SHARED RAYLIB_RELEASE_PATH=../../../bin RAYLIB_BUILD_MODE=DEBUG
	cd ../../..
)


:: Build rl.c
cmd /c if exist bin\rl.exe del /F bin\rl.exe
gcc %CFLAGS% %DEV_FLAGS% -o bin/rl src/main.c %DEPS%