param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'prepare-core.ps1')
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
if (!$installation) { throw 'MSBuildが見つかりません。' }
& (Join-Path $installation 'MSBuild\Current\Bin\amd64\MSBuild.exe') (Join-Path $projectRoot 'WiizV.sln') /t:WiizVCore "/p:Configuration=$Configuration" /p:Platform=x64 /p:PreferredToolArchitecture=x64 /m:1 /nr:false /v:minimal
if ($LASTEXITCODE -ne 0) { throw "コアのビルド失敗: $LASTEXITCODE" }
