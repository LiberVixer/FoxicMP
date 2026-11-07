[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$ToolchainPrefix,
 [Parameter(Mandatory=$true)][string]$WorkDirectory,
 [Parameter(Mandatory=$true)][string]$SourceArchive,
 [Parameter(Mandatory=$true)][string]$SourceOverlay,
 [Parameter(Mandatory=$true)][string]$CppWinRTArchive,
 [int]$Jobs=3
)
$ErrorActionPreference='Stop'
$r=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$t=[IO.Path]::GetFullPath($ToolchainPrefix).Replace('\','/')
$base=[IO.Path]::GetFullPath($WorkDirectory).Replace('\','/')
$archive=[IO.Path]::GetFullPath($SourceArchive);$overlay=[IO.Path]::GetFullPath($SourceOverlay)
$env:PATH="$t/bin;$env:SystemRoot/System32;$env:SystemRoot"
$null=New-Item $base -ItemType Directory -Force
$meta=Get-Content "$r/tools/runtime/qt-6.12.0-long-lock-paths.json" -Raw|ConvertFrom-Json
if((Get-FileHash $archive -Algorithm SHA256).Hash.ToLower() -ne $meta.archiveSha256){throw 'Qt archive hash'}
if((Get-FileHash $overlay -Algorithm SHA256).Hash.ToLower() -ne $meta.overlaySha256){throw 'Qt overlay hash'}
if(!(Test-Path "$base/source/qtbase-everywhere-src-6.12.0")){
 New-Item "$base/source" -ItemType Directory -Force|Out-Null
 & "$env:SystemRoot/System32/tar.exe" -xf $archive -C "$base/source"
 if($LASTEXITCODE){throw 'Qt source extraction'}
 & "$env:SystemRoot/System32/tar.exe" -xf $overlay -C "$base/source"
 if($LASTEXITCODE){throw 'Qt source overlay'}
}
$src="$base/source/qtbase-everywhere-src-6.12.0"
if((Get-FileHash "$src/src/corelib/io/qlockfile_win.cpp" -Algorithm SHA256).Hash.ToLower() -ne $meta.qlockfileAfterSha256){throw 'Qt patched source hash'}
if((Get-FileHash "$src/src/corelib/io/qfilesystemengine_win.cpp" -Algorithm SHA256).Hash.ToLower() -ne $meta.tempPathAfterSha256){throw 'Qt temporary-path source hash'}
$deps="$base/deps"
if((Get-FileHash $CppWinRTArchive -Algorithm SHA256).Hash.ToLower() -ne $meta.cppWinRT.sha256){throw 'CppWinRT archive hash'}
if(!(Test-Path "$deps/ucrt64/include/winrt/base.h")){
 New-Item $deps -ItemType Directory -Force|Out-Null
 & "$env:SystemRoot/System32/tar.exe" -xf $CppWinRTArchive -C $deps
 if($LASTEXITCODE){throw 'CppWinRT private headers extraction'}
}
$env:PKG_CONFIG_PATH="$t/lib/pkgconfig;$t/share/pkgconfig";$env:PKG_CONFIG_EXECUTABLE="$t/bin/pkg-config.exe"
$ErrorActionPreference='Continue'
& "$t/bin/cmake.exe" -S $src -B "$base/build" -G Ninja "-DCMAKE_C_COMPILER=$t/bin/gcc.exe" "-DCMAKE_CXX_COMPILER=$t/bin/g++.exe" "-DCMAKE_BUILD_TYPE=RelWithDebInfo" "-DCMAKE_CXX_FLAGS=-I$deps/ucrt64/include" -U*cpp_winrt* "-DCMAKE_PREFIX_PATH=$t" "-DCMAKE_INSTALL_PREFIX=$base/install" -DINSTALL_INCLUDEDIR=include/qt6 -DINSTALL_ARCHDATADIR=share/qt6 -DINSTALL_DATADIR=share/qt6 -DINSTALL_MKSPECSDIR=share/qt6/mkspecs -DINSTALL_DESCRIPTIONSDIR=share/qt6/modules -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DQT_QMAKE_TARGET_MKSPEC=win32-g++ -DFEATURE_system_doubleconversion=ON -DFEATURE_system_freetype=ON -DFEATURE_system_jpeg=ON -DFEATURE_system_harfbuzz=ON -DFEATURE_system_pcre2=ON -DFEATURE_system_png=ON -DFEATURE_system_sqlite=ON -DFEATURE_system_zlib=ON -DFEATURE_zstd=ON -DFEATURE_openssl=ON -DFEATURE_opengl=ON -DFEATURE_opengl_desktop=ON -DFEATURE_egl=OFF -DFEATURE_dbus=ON -DFEATURE_glib=ON -DFEATURE_icu=ON -DFEATURE_pkg_config=ON -DFEATURE_vulkan=ON -DFEATURE_gtk3=OFF -DFEATURE_regularexpression=ON -DFEATURE_cxx20=ON -DFEATURE_windows_ioring=ON -DFEATURE_cpp_winrt=ON -DFEATURE_force_debug_info=ON -DFEATURE_separate_debug_info=ON *> "$base/configure.log"
$code=$LASTEXITCODE;[IO.File]::WriteAllText("$base/configure.exit",[string]$code);if($code){exit $code}
& "$t/bin/cmake.exe" --build "$base/build" --target Core --parallel $Jobs *> "$base/build.log"
$code=$LASTEXITCODE;[IO.File]::WriteAllText("$base/build.exit",[string]$code);if($code){exit $code}
$ErrorActionPreference='Stop'
& "$t/bin/python3.exe" "$r/tools/runtime/validate_qtcore_runtime.py" --toolchain-prefix $t --work $base
if($LASTEXITCODE){throw 'Private QtCore validation failed'}
