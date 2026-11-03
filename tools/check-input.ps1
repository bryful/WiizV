param([Parameter(Mandatory=$true)][string]$Rom, [ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$testDirectory = Join-Path $projectRoot ('verification\input-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $testDirectory | Out-Null
$arguments = '--smoke --input-check "{0}" --data-dir "{1}" --frames 900' -f $romPath,$testDirectory
$process = Start-Process -FilePath (Join-Path $projectRoot "x64\$Configuration\WiizV.exe") -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
$report = Join-Path $testDirectory 'input-check-report.txt'
if (Test-Path -LiteralPath $report) { Get-Content -LiteralPath $report }
if ($process.ExitCode -ne 0) {
    Get-Content -LiteralPath (Join-Path $projectRoot "x64\$Configuration\last-error.txt")
    throw "入力検証失敗: $($process.ExitCode)"
}
Write-Output "PASS: 実際の城メニューで長押し・短い3回の押下を全倍率で確認 ($testDirectory)"
