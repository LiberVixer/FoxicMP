@echo off
rem Run in an x64 MSVC Developer Command Prompt, after building FoxicMP Classic
rem and its static dependencies. No installed player or system renderer is used.
cd /d "%~dp0..\.."
if not exist _bin\classic-pcm-graph-probe mkdir _bin\classic-pcm-graph-probe
powershell.exe -NoProfile -Command "[IO.File]::WriteAllText('%CD%\_bin\classic-pcm-graph-probe\probe.ini',\"[Settings]`r`nContextMenuPrompted=1`r`nContextMenuEnabled=0`r`n\",[Text.Encoding]::Unicode)"
if errorlevel 1 exit /b %ERRORLEVEL%
MSBuild.exe src\filters\transform\MpaDecFilter\MpaDecFilter.vcxproj /nologo /m:3 /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%CD%\ /p:BuildProjectReferences=false /v:minimal
if errorlevel 1 exit /b %ERRORLEVEL%
cl.exe /nologo /std:c++17 /EHsc /O2 /MT /D_UNICODE /DUNICODE /DNOMINMAX /DNDEBUG /D_HAS_STD_BYTE=0 /I include /I src /I src\ExtLib\ffmpeg tests\windows\classic_mp3_graph_probe.cpp /Fo_bin\classic-pcm-graph-probe.obj /Fe_bin\classic-pcm-graph-probe\probe.exe /link /incremental:no /LIBPATH:_bin\lib\Release_x64 /LIBPATH:lib64 MP4Splitter.lib BaseSplitter.lib Bento4.lib MpaDecFilter.lib AudioTools.lib DSUtil.lib Filters.lib BaseClasses.lib DeCSSFilterStatic.lib ffmpeg.lib libsoxr.lib speex.lib zlib.lib uavs3d.lib vvdec.lib libucrt.lib libgcc.a libmingwex.a libdav1d.a fdk-aac.lib strmiids.lib winmm.lib ws2_32.lib bcrypt.lib /ignore:4099
exit /b %ERRORLEVEL%
