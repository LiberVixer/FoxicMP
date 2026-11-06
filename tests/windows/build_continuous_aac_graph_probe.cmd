@echo off
rem x64 MSVC Developer Command Prompt; prebuilt Classic dependencies required.
rem Recompile changed objects privately, leaving existing static libraries intact.
cd /d "%~dp0..\.."
if not exist _bin\continuous-aac-classic mkdir _bin\continuous-aac-classic
powershell.exe -NoProfile -Command "foreach($n in @('probe','seek-oracle')){[IO.File]::WriteAllText(('%CD%\_bin\continuous-aac-classic\'+$n+'.ini'),\"[Settings]`r`nContextMenuPrompted=1`r`nContextMenuEnabled=0`r`n\",[Text.Encoding]::Unicode)}"
if errorlevel 1 exit /b %ERRORLEVEL%
set "AAC_TEST_FLAGS=/nologo /std:c++17 /EHsc /O2 /MT /D_UNICODE /DUNICODE /DNOMINMAX /DNDEBUG /D_USE_MATH_DEFINES /D_HAS_STD_BYTE=0 /I include /I src /I src\ExtLib\ffmpeg /I src\ExtLib\mfx_dispatch\include"
cl.exe %AAC_TEST_FLAGS% /c src\DSUtil\HarvesterSession.cpp /Fo_bin\continuous-aac-classic\HarvesterSession.obj
if errorlevel 1 exit /b %ERRORLEVEL%
cl.exe %AAC_TEST_FLAGS% /c src\filters\parser\MP4Splitter\MP4Splitter.cpp /Fo_bin\continuous-aac-classic\MP4Splitter.obj
if errorlevel 1 exit /b %ERRORLEVEL%
cl.exe %AAC_TEST_FLAGS% /c src\filters\transform\MpaDecFilter\MpaDecFilter.cpp /Fo_bin\continuous-aac-classic\MpaDecFilter.obj
if errorlevel 1 exit /b %ERRORLEVEL%
cl.exe %AAC_TEST_FLAGS% /c src\filters\transform\MpaDecFilter\FFAudioDecoder.cpp /Fo_bin\continuous-aac-classic\FFAudioDecoder.obj
if errorlevel 1 exit /b %ERRORLEVEL%
cl.exe %AAC_TEST_FLAGS% tests\windows\continuous_aac_graph_probe.cpp /Fo_bin\continuous-aac-classic\probe.obj /Fe_bin\continuous-aac-classic\probe.exe /link _bin\continuous-aac-classic\HarvesterSession.obj _bin\continuous-aac-classic\MP4Splitter.obj _bin\continuous-aac-classic\MpaDecFilter.obj _bin\continuous-aac-classic\FFAudioDecoder.obj /incremental:no /LIBPATH:_bin\lib\Release_x64 /LIBPATH:lib64 MP4Splitter.lib BaseSplitter.lib Bento4.lib MPCVideoDec.lib BaseVideoFilter.lib MpaDecFilter.lib AudioTools.lib DSUtil.lib Filters.lib BaseClasses.lib DeCSSFilterStatic.lib ffmpeg.lib mfx_dispatch.lib libsoxr.lib speex.lib zlib.lib uavs3d.lib vvdec.lib libucrt.lib libgcc.a libmingwex.a libdav1d.a fdk-aac.lib strmiids.lib winmm.lib ws2_32.lib bcrypt.lib d3d11.lib dxgi.lib d3d9.lib dxguid.lib /ignore:4099
if errorlevel 1 exit /b %ERRORLEVEL%
cl.exe %AAC_TEST_FLAGS% tests\windows\classic_aac_pcm_probe.cpp /Fo_bin\continuous-aac-classic\seek-oracle.obj /Fe_bin\continuous-aac-classic\seek-oracle.exe /link _bin\continuous-aac-classic\HarvesterSession.obj _bin\continuous-aac-classic\MP4Splitter.obj _bin\continuous-aac-classic\MpaDecFilter.obj _bin\continuous-aac-classic\FFAudioDecoder.obj /incremental:no /LIBPATH:_bin\lib\Release_x64 /LIBPATH:lib64 MP4Splitter.lib BaseSplitter.lib Bento4.lib MPCVideoDec.lib BaseVideoFilter.lib MpaDecFilter.lib AudioTools.lib DSUtil.lib Filters.lib BaseClasses.lib DeCSSFilterStatic.lib ffmpeg.lib mfx_dispatch.lib libsoxr.lib speex.lib zlib.lib uavs3d.lib vvdec.lib libucrt.lib libgcc.a libmingwex.a libdav1d.a fdk-aac.lib strmiids.lib winmm.lib ws2_32.lib bcrypt.lib d3d11.lib dxgi.lib d3d9.lib dxguid.lib /ignore:4099
exit /b %ERRORLEVEL%
