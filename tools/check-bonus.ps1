param([Parameter(Mandatory=$true)][string]$Rom, [ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$testDirectory = Join-Path $projectRoot ('verification\bonus-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $testDirectory | Out-Null
$executable = Join-Path $projectRoot "x64\$Configuration\WiizV.exe"
$arguments = '--smoke --bonus-check "{0}" --data-dir "{1}" --frames 900' -f $romPath,$testDirectory
$process = Start-Process -FilePath $executable -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) {
    Get-Content -LiteralPath (Join-Path $projectRoot "x64\$Configuration\last-error.txt")
    throw "ボーナス検証失敗: $($process.ExitCode)"
}
Get-Content -LiteralPath (Join-Path $testDirectory 'bonus-check-report.txt')
$state = Join-Path $testDirectory ('States\' + [IO.Path]::GetFileNameWithoutExtension($romPath) + '.slot1.wvs')
$arguments = '--smoke --bonus-resume-check "{0}" --data-dir "{1}" --start-state "{2}" --frames 60' -f $romPath,$testDirectory,$state
$process = Start-Process -FilePath $executable -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "ボーナス設定の再起動検証失敗: $($process.ExitCode)" }
Get-Content -LiteralPath (Join-Path $testDirectory 'bonus-resume-report.txt')
Write-Output "検証結果: $testDirectory"
