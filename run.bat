:: Run "run.bat r" to remove the data directory before running rl.exe

@echo off
pushd bin
if "%~1"=="r" (
	del /Q data
)
call main.exe
popd ..
