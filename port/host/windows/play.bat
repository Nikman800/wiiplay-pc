@echo off
rem Wii Play (PC port): plays the extracted disc in the "disc" folder next to this file,
rem in a window. Other options can follow, for example: play.bat --fullscreen
"%~dp0wiiplay.exe" --disc "%~dp0disc" --window %*
if errorlevel 1 pause
