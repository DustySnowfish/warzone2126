@echo off
rem Warzone 2126 - Large battle stress test.
rem Runs the "battle" visual benchmark through run_visual_benchmark.bat (same results folder).
call "%~dp0run_visual_benchmark.bat" battle %1
