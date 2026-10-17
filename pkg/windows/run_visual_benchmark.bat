@echo off
rem Warzone 2126 - run the vertical slice visual benchmark.
rem Plays a fixed AI-vs-AI match, flies the camera through set shots,
rem records frame timings and screenshots, then opens the results folder.
setlocal
cd /d "%~dp0"
set "GAMEEXE="
for %%E in (warzone2126.exe warzone2100.exe) do if not defined GAMEEXE if exist "%%E" set "GAMEEXE=%%E"
if not defined GAMEEXE (
  echo Could not find the game executable next to this script.
  pause
  exit /b 1
)
set "BENCH=%~1"
if "%BENCH%"=="" set "BENCH=slice"
rem Fixed window size so results are comparable between runs and machines.
set "RES=%~2"
if "%RES%"=="" set "RES=1920x1080"
echo Running visual benchmark "%BENCH%" at %RES% with %GAMEEXE% ...
echo The game will start, play itself for about two minutes, then close.
"%GAMEEXE%" --visualbench=%BENCH% --resolution=%RES% --window > "%TEMP%\wz2126_bench_output.txt" 2>&1
set "RESULTS="
for /f "tokens=2,*" %%A in ('findstr /b /c:"WZBENCH: results" "%TEMP%\wz2126_bench_output.txt"') do set "RESULTS=%%B"
findstr /b /c:"WZBENCH: overall" "%TEMP%\wz2126_bench_output.txt"
if defined RESULTS (
  echo Results: %RESULTS%
  for %%F in ("%RESULTS%") do start "" explorer "%%~dpF"
) else (
  echo Opening the benchmarks folder in your Warzone 2126 settings directory.
  for /d %%D in ("%APPDATA%\Warzone 2126 Project\Warzone 2126*") do if exist "%%D\benchmarks" start "" explorer "%%D\benchmarks"
  pause
)
endlocal
