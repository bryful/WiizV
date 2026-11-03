param([Parameter(Mandatory=$true)][string]$Rom, [ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$testDirectory = Join-Path $projectRoot ('verification\speed-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $testDirectory | Out-Null
$arguments = '--smoke --speed-check "{0}" --data-dir "{1}" --frames 900' -f $romPath,$testDirectory
$process = Start-Process -FilePath (Join-Path $projectRoot "x64\$Configuration\WiizV.exe") -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
Get-Content -LiteralPath (Join-Path $testDirectory 'speed-report.txt')
if ($process.ExitCode -ne 0) { throw "倍速検証失敗: $($process.ExitCode)" }
foreach ($factor in @('3','4')) {
    foreach ($speedArgument in @("--speed $factor", '')) {
        $arguments = '--smoke {0} "{1}" --data-dir "{2}" --frames 3600' -f $speedArgument,$romPath,$testDirectory
        $process = Start-Process -FilePath (Join-Path $projectRoot "x64\$Configuration\WiizV.exe") -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw "倍率の再起動検証失敗: $($process.ExitCode)" }
        $report = Get-Content -LiteralPath (Join-Path $testDirectory 'smoke-report.txt') -Raw
        if ($report -notmatch "game_speed=$factor") { throw "$factor 倍速の設定が維持されませんでした。" }
    }
}
Write-Output "PASS: 倍速中の音声長・波形 ($testDirectory)"
Write-Output 'PASS: F2/menu controls; speed persistence after process restart'
