[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$ToolchainDirectory,
  [Parameter(Mandatory=$true)][string]$BuildDirectory,
  [int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
# CMake persists compiler paths in generated CMake files. Backslashes such as
# \ucrt64 are interpreted as escape sequences there; use forward slashes.
$toolchain = (Resolve-Path -LiteralPath $ToolchainDirectory).Path.Replace('\', '/')
$prefix = "$toolchain/ucrt64"
$bin = "$prefix/bin"
$env:PATH = "$bin;$env:PATH"
$source = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path.Replace('\', '/')
& "$bin/cmake.exe" -S $source -B $BuildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "-DCMAKE_PREFIX_PATH=$prefix" "-DCMAKE_CXX_COMPILER=$bin/g++.exe" "-DCMAKE_RC_COMPILER=$bin/windres.exe"
if ($LASTEXITCODE) { throw 'CMake configure failed' }
& "$bin/cmake.exe" --build $BuildDirectory -j $Jobs
if ($LASTEXITCODE) { throw 'Build failed' }
