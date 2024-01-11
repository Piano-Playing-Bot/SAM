PROD_FLAGS="-O2 -s"
DEV_FLAGS="-g -ggdb"
CFLAGS="-Wall -Wextra -Wimplicit -Wpedantic -Wno-unused-function -std=c99"

LIB_PATHS="-L./bin"
INCLUDES="-I./deps/tsoding -I./deps/raylib/src -I./deps/ail"
RAYLIB_DEP="-l:libraylib.a -lm -lpthread"
DEPS="$INCLUDES $LIB_PATHS $RAYLIB_DEP $RAYGUI_DEP"

if [[ $1 -eq "a" ]] || [ -d "./bin" ]; then
	# Remove old bin folder
	rm -rf "./bin"
	mkdir "./bin"
	cp -r "./assets" "./bin/assets"
	# Build raylib
	cd deps/raylib/src
	make PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC RAYLIB_RELEASE_PATH=../../../bin RAYLIB_BUILD_MODE=DEBUG
	cd ../../..
fi

if [ -f "./bin/main" ]; then
	rm -f "./bin/main"
fi
gcc $CFLAGS $DEV_FLAGS -o bin/main src/main.c $DEPS