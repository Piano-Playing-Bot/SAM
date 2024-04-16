# Self-Applying Musician (SAM)

SAM is the UI application for the self-playing piano player.

## Quickstart

Prerequesites to building this project `gcc` and `make`, which both need to be installed and in the path.

You also need to make sure to have the [common](https://github.com/Piano-Playing-Bot/common) repository situated next to this directory.

To build the project, simply run `make clean main`.

This creates the folder `bin/` in the root directory and puts the binary along with all required assets in there. All songs, that are added during the program's run are also stored in this directory.

To recompile without losing the music data in the `bin/` folder from previous usage, run `make main` instead.

To run the application, you can either go into the `bin/` directory and run `main.exe` from there, or you can call `run.bat` (or `run.sh` on Linux) from the root directory.

## Code Layout

The entire source code for SAM is located in `src/` and is split between four files:

- main.c contains all the code for the UI thread
- midi.c contains all the code for parsing MIDI files and transforming them into PIDI files
- comm.c contains all the code for the Communications thread, that communicates with the Arduino for playing the music
- header.h contains common includes and defines that are shared between all three files

The `utils/` folder contains several scripts for testing purposes. Each of them can be built by calling `make <name_of_file>`. The executable is then built into the root directory and can be run from there.

The `midis/` folder contains several midi files that were used for testing purposes.

The `deps/` folder contains all third-party dependencies used by this application. The only such dependency is the library [Raylib](https://www.raylib.com/), which provides a cross-platform rendering abstraction.

All other dependencies (that were written by myself) are contained within the [common](https://github.com/Piano-Playing-Bot/common) repository.

The `assets/` folder contains all assets used by the application.
