:: Run "run.bat r" to remove the data directory before running rl.exe

@call make main
@echo off
if %errorLevel%==0 (
	pushd bin
	if "%~1"=="r" (
		del /Q data
	)
	call main.exe
	popd ..
)
