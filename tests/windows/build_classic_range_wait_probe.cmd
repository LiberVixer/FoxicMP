@echo off
rem x64 MSVC Developer Command Prompt; normal Classic static dependencies built.
cd /d "%~dp0..\.."
set "CRB_DIR=%~1"
if "%CRB_DIR%"=="" set "CRB_DIR=_bin\classic-range-wait-probe"
if not exist "%CRB_DIR%" mkdir "%CRB_DIR%"
set "CRB_FLAGS=/nologo /std:c++17 /EHsc /utf-8 /O2 /MT /D_UNICODE /DUNICODE /DNOMINMAX /DNDEBUG /D_USE_MATH_DEFINES /D_HAS_STD_BYTE=0 /DFOXICMP_PRIVATE_CLASSIC_READ_TEST=1 /I include /I src /I src\ExtLib\ffmpeg /I src\ExtLib\mfx_dispatch\include"
cl.exe %CRB_FLAGS% /c src\DSUtil\HarvesterSession.cpp /Fo"%CRB_DIR%\HarvesterSession.obj"
if errorlevel 1 exit /b %ERRORLEVEL%
cl.exe %CRB_FLAGS% /c src\filters\parser\BaseSplitter\AsyncReader.cpp /Fo"%CRB_DIR%\AsyncReader.obj"
if errorlevel 1 exit /b %ERRORLEVEL%
cl.exe %CRB_FLAGS% tests\windows\classic_range_wait_probe.cpp /Fo"%CRB_DIR%\wait.obj" /Fe"%CRB_DIR%\wait.exe" /link "%CRB_DIR%\HarvesterSession.obj" "%CRB_DIR%\AsyncReader.obj" /incremental:no /LIBPATH:_bin\lib\Release_x64 /LIBPATH:lib64 MP4Splitter.lib BaseSplitter.lib Bento4.lib MPCVideoDec.lib BaseVideoFilter.lib DSUtil.lib Filters.lib BaseClasses.lib DeCSSFilterStatic.lib ffmpeg.lib mfx_dispatch.lib libsoxr.lib speex.lib zlib.lib uavs3d.lib vvdec.lib libucrt.lib libgcc.a libmingwex.a libdav1d.a fdk-aac.lib strmiids.lib winmm.lib ws2_32.lib bcrypt.lib d3d11.lib dxgi.lib d3d9.lib dxguid.lib /ignore:4099
if errorlevel 1 exit /b %ERRORLEVEL%
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -Command "[IO.File]::WriteAllText((Join-Path (Resolve-Path '%CRB_DIR%') 'wait.ini'),'[Settings]'+[Environment]::NewLine+'ContextMenuPrompted=1'+[Environment]::NewLine+'ContextMenuEnabled=0'+[Environment]::NewLine,[Text.Encoding]::Unicode)"
exit /b %ERRORLEVEL%
