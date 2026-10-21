@echo off
rem Warzone 2126 - Two armies crossing a 512-tile map (about 2 minutes).
rem Runs the "bigmap" visual benchmark through run_visual_benchmark.bat (same results folder).
call "%~dp0run_visual_benchmark.bat" bigmap %1
