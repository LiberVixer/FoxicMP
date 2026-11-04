@echo off
rem x64 MSVC Developer Command Prompt, Classic static dependencies already built.
cd /d "%~dp0..\.."
if not exist _bin\continuous-graph-probe mkdir _bin\continuous-graph-probe
powershell.exe -NoProfile -Command "[IO.File]::WriteAllText('%CD%\_bin\continuous-graph-probe\probe.ini',\"[Settings]`r`nContextMenuPrompted=1`r`nContextMenuEnabled=0`r`n\",[Text.Encoding]::Unicode)"
if errorlevel 1 exit /b %ERRORLEVEL%
MSBuild.exe src\DSUtil\DSUtil.vcxproj /nologo /m:3 /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%CD%\ /p:BuildProjectReferences=false /v:minimal
if errorlevel 1 exit /b %ERRORLEVEL%
MSBuild.exe src\filters\parser\BaseSplitter\BaseSplitter.vcxproj /nologo /m:3 /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%CD%\ /p:BuildProjectReferences=false /v:minimal
if errorlevel 1 exit /b %ERRORLEVEL%
MSBuild.exe src\filters\parser\MP4Splitter\MP4Splitter.vcxproj /nologo /m:3 /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%CD%\ /p:BuildProjectReferences=false /v:minimal
if errorlevel 1 exit /b %ERRORLEVEL%
cl.exe /nologo /std:c++17 /EHsc /O2 /MT /D_UNICODE /DUNICODE /DNOMINMAX /DNDEBUG /D_HAS_STD_BYTE=0 /I include /I src /I src\ExtLib\ffmpeg /I src\ExtLib\mfx_dispatch\include tests\windows\continuous_graph_probe.cpp /Fo_bin\continuous-graph-probe\probe.obj /Fe_bin\continuous-graph-probe\probe.exe /link /incremental:no /LIBPATH:_bin\lib\Release_x64 /LIBPATH:lib64 MP4Splitter.lib BaseSplitter.lib Bento4.lib MPCVideoDec.lib BaseVideoFilter.lib DSUtil.lib Filters.lib BaseClasses.lib DeCSSFilterStatic.lib ffmpeg.lib mfx_dispatch.lib libsoxr.lib speex.lib zlib.lib uavs3d.lib vvdec.lib libucrt.lib libgcc.a libmingwex.a libdav1d.a fdk-aac.lib strmiids.lib winmm.lib ws2_32.lib bcrypt.lib d3d11.lib dxgi.lib d3d9.lib dxguid.lib /ignore:4099
exit /b %ERRORLEVEL%
