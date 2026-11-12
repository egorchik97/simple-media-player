@echo off
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b 1
cl /std:c++20 /utf-8 /O2 /EHsc /W4 "D:\vibe code\злеир\MediaPopup.cpp" "D:\vibe code\злеир\Asr.cpp" user32.lib gdi32.lib windowsapp.lib /link /SUBSYSTEM:WINDOWS /OUT:"D:\vibe code\злеир\MediaPopup.exe"
if errorlevel 1 exit /b 1
cl /std:c++20 /utf-8 /O2 /EHsc /W4 "D:\vibe code\злеир\MediaPopupTests.cpp" "D:\vibe code\злеир\Asr.cpp" user32.lib gdi32.lib windowsapp.lib /link /SUBSYSTEM:CONSOLE /OUT:"D:\vibe code\злеир\MediaPopupTests.exe"
if errorlevel 1 exit /b 1
cl /std:c++20 /utf-8 /O2 /EHsc /W4 AsrTests.cpp Asr.cpp /Fe:AsrTests.exe
if errorlevel 1 exit /b 1
cl /std:c++20 /utf-8 /O2 /EHsc /W4 MediaProbe.cpp Asr.cpp user32.lib gdi32.lib windowsapp.lib /link /SUBSYSTEM:CONSOLE /OUT:MediaProbe.exe
if errorlevel 1 exit /b 1
"%~dp0MediaPopupTests.exe"
if errorlevel 1 exit /b 1
"%~dp0AsrTests.exe"
if errorlevel 1 exit /b 1
cl /std:c++20 /utf-8 /O2 /EHsc /W4 LyricsTests.cpp windowsapp.lib /Fe:LyricsTests.exe
if errorlevel 1 exit /b 1
"%~dp0LyricsTests.exe"
exit /b %errorlevel%

