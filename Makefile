# Define MODE=RELEASE if compiling in release mode

CC        = gcc
MODE     ?= DEBUG
CFLAGS   ?= -Wall -Wextra -Wpedantic -std=c99 -Wno-unused-function -Wno-unused-local-typedefs -DDEBUG -D_DEBUG -DUI_DEBUG

COMMON_PATH = ../common/

ifeq ($(MODE), RELEASE)
CFLAGS += -O2 -mwindows -s
export RAYLIB_BUILD_MODE=RELEASE
else
CFLAGS += -ggdb
export RAYLIB_BUILD_MODE=DEBUG
endif

INCLUDES  = -I./src -I$(COMMON_PATH) -I./deps/raylib/src -I$(COMMON_PATH)ail
LIBS      = -L./bin -lraylib -lopengl32 -lgdi32 -lwinmm -lpthread -lwinspool
CFLAGS   += $(INCLUDES) $(LIBS)


.PHONY: clean main pidi_test midi_test print_bin pidi_maker show_pidi

all: main pidi_test midi_test print_bin pidi_maker show_pidi

main: bin/libraylib.a src/main.c src/midi.c src/comm.c
	$(CC) -o bin/main src/main.c $(CFLAGS)

pidi_test: utils/pidi_test.c
	$(CC) -o pidi_test utils/pidi_test.c $(CFLAGS)

midi_test: utils/midi_test.c
	$(CC) -o midi_test utils/midi_test.c $(CFLAGS)

print_bin: utils/print_bin.c
	$(CC) -o print_bin utils/print_bin.c $(CFLAGS)

pidi_maker: utils/pidi_maker.c
	$(CC) -o pidi_maker utils/pidi_maker.c $(CFLAGS)

show_pidi: utils/show_pidi.c
	$(CC) -o show_pidi utils/show_pidi.c $(CFLAGS)

export PLATFORM=PLATFORM_DESKTOP
export RAYLIB_LIBTYPE=STATIC
export RAYLIB_RELEASE_PATH=../../../bin
bin/libraylib.a:
	$(MAKE) -C deps/raylib/src

clean:
ifeq ($(OS),Windows_NT)
	cmd /c if exist "bin" cmd /c rmdir bin /S /Q
	mkdir bin
	xcopy assets bin\\assets\\ /E /Q
else
	rm -rf ./bin
	mkdir ./bin
	cp -r "./assets" "./bin/assets"
endif