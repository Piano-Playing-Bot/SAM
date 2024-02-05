# Define MODE=RELEASE if compiling in release mode

CC        = gcc
MODE     ?= DEBUG
CFLAGS   ?= -Wall -Wextra -Wpedantic -std=c99 -Wno-unused-function -std=c99

COMMON_PATH = ../common/

ifeq ($(MODE), RELEASE)
CFLAGS += -O2 -mwindows -s
export RAYLIB_BUILD_MODE=RELEASE
else
CFLAGS += -ggdb
export RAYLIB_BUILD_MODE=DEBUG
endif

INCLUDES  = -I$(COMMON_PATH) -I./deps/raylib/src -I$(COMMON_PATH)ail
LIBS      = -L./bin -lraylib -lopengl32 -lgdi32 -lwinmm -lpthread -lwinspool
CFLAGS   += $(INCLUDES) $(LIBS)


.PHONY: clean main

all: main

main: src/main.c raylib
	$(CC) -o bin/main src/main.c $(CFLAGS)

export PLATFORM=PLATFORM_DESKTOP
export RAYLIB_LIBTYPE=STATIC
export RAYLIB_RELEASE_PATH=../../../bin
raylib:
	$(MAKE) -C deps/raylib/src

clean:
# ifeq ($(OS),Windows_NT)
# 	rmdir bin /S /Q
# 	mkdir bin
# 	xcopy assets bin\assets\ /E /Q
# else
	rm -rf ./bin
	mkdir ./bin
	cp -r "./assets" "./bin/assets"
# endif