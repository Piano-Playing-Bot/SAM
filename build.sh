PROD_FLAGS="-O2 -s"
DEV_FLAGS="-g -ggdb"
CFLAGS="-Wall -Wextra -Wimplicit -Wpedantic -Wno-unused-function -std=c99"


LIB_PATHS="-L./bin"
INCLUDES="-I./deps/stb -I./deps/tsoding -I./deps/QuelSolaar -I./deps/raylib/src -I./deps/raygui/src"
RAYLIB_DEP="-lraylib -lopengl32 -lgdi32 -lwinmm"
RAYGUI_DEP="-lraygui"
DEPS="$INCLUDES $LIB_PATHS $RAYLIB_DEP $RAYGUI_DEP"

if [[ $1 -eq "a" ]] || [ -d "./bin" ]; then
	# Remove old bin folder
	rm -rf "./bin"
	mkdir "./bin"
	cp -r "./assets" "./bin/assets"
	# Build raylib
	cd deps/raylib/src
	make PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=SHARED RAYLIB_RELEASE_PATH=../../../bin RAYLIB_BUILD_MODE=DEBUG
	cd ../../..
	# Build raygui
	gcc -o bin/raygui.dll deps/raygui/src/raygui.c -shared -DRAYGUI_IMPLEMENTATION -DBUILD_LIBTYPE_SHARED -static-libgcc -Wl,--out-implib,bin/librayguidll.a $RAYLIB_DEP $INCLUDES $LIB_PATHS
fi

if [ -f "./bin/rl" ]; then
	rm -f "./bin/rl"
fi
gcc $CFLAGS $DEV_FLAGS -o bin/rl src/main.c $DEPS