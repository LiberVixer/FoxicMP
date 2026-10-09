@echo off
setlocal
rem Run from repository root in a Visual Studio x64 developer command prompt, after building Release x64.
if not exist _bin\harvester-test mkdir _bin\harvester-test
cl /nologo /std:c++17 /EHsc /O2 /MT /Isrc tests\harvester\bento_head_tests.cpp /Fe:_bin\harvester-test\bento-head-tests.exe /Fo:_bin\harvester-test\bento-head-tests.obj /link /LTCG _bin\lib\Release_x64\Bento4.lib _bin\lib\Release_x64\zlib.lib
if errorlevel 1 exit /b 1
rem Supply your generated video-fragmented.mp4 and video.mp4.
_bin\harvester-test\bento-head-tests.exe %1 %2
exit /b %errorlevel%
