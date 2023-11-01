@echo off
call gcc -o midi midi.c -Wall -Wextra -Wpedantic -Wimplicit -std=c99 -g -ggdb -I../deps/ail
call midi.exe