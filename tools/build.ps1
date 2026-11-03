param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'prepare-core.ps1')
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
if (!$installation) { throw 'Visual Studio/MSBuildが見つかりません。' }
$msbuild = Join-Path $installation 'MSBuild\Current\Bin\amd64\MSBuild.exe'
& $msbuild (Join-Path $projectRoot 'WiizV.sln') "/p:Configuration=$Configuration" /p:Platform=x64 /p:PreferredToolArchitecture=x64 /m:1 /nr:false /v:minimal
if ($LASTEXITCODE -ne 0) { throw "ビルド失敗: $LASTEXITCODE" }
