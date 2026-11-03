param([Parameter(Mandatory=$true)][string]$Rom, [ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$testDirectory = Join-Path $projectRoot ('verification\movement-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $testDirectory | Out-Null
$arguments = '--smoke --movement-check "{0}" --data-dir "{1}" --frames 900' -f $romPath,$testDirectory
$process = Start-Process -FilePath (Join-Path $projectRoot "x64\$Configuration\WiizV.exe") -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) {
    Get-Content -LiteralPath (Join-Path $projectRoot "x64\$Configuration\last-error.txt")
    throw "移動検証失敗: $($process.ExitCode)"
}
Get-Content -LiteralPath (Join-Path $testDirectory 'movement-check-report.txt')
Write-Output "PASS: 移動検証 ($testDirectory)"
