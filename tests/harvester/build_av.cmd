@echo off
rem Run from repository root in a Visual Studio x64 developer command prompt.
if not exist _bin\harvester-test mkdir _bin\harvester-test
cl /nologo /std:c++17 /EHsc /O2 /utf-8 /DUNICODE /D_UNICODE tests\harvester\windows_av_capture.cpp /Fe:_bin\harvester-test\av-capture.exe /Fo:_bin\harvester-test\av-capture.obj /link ole32.lib mmdevapi.lib user32.lib gdi32.lib
exit /b %errorlevel%
