@echo off

:: -mwindows : Compile a Windows executable, no cmd window
set PROD_FLAGS=-O2 -mwindows -s
set DEV_FLAGS=-g -ggdb
set CFLAGS=-Wall -Wextra -Wimplicit -Wpedantic -Wno-unused-function -std=c99

:: Compiler-Options to include dependencies
set LIB_PATHS=-L./bin "-L/Program Files/vcpkg/packages/libusb_x64-windows/lib"
set INCLUDES=-I./deps/tsoding -I./deps/raylib/src -I./deps/ail "-I/Program Files/vcpkg/packages/libusb_x64-windows/include/libusb-1.0"
set RAYLIB_DEP=-lraylib -lopengl32 -lgdi32 -lwinmm -lpthread
set DEPS=%INCLUDES% %LIB_PATHS% %RAYLIB_DEP% -llibusb-1.0

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


:: Build executable
cmd /c if exist bin\main.exe del /F bin\main.exe
gcc %CFLAGS% %DEV_FLAGS% -o bin/main src/main.c %DEPS%