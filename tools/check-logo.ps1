param([Parameter(Mandatory=$true)][string]$Rom, [ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$testDirectory = Join-Path $projectRoot ('verification\logo-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $testDirectory | Out-Null
$executable = Join-Path $projectRoot "x64\$Configuration\WiizV.exe"
$arguments = '--smoke --logo-check "{0}" --data-dir "{1}" --frames 900' -f $romPath,$testDirectory
$process = Start-Process -FilePath $executable -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) {
    Get-Content -LiteralPath (Join-Path $projectRoot "x64\$Configuration\last-error.txt")
    throw "ロゴ検証失敗: $($process.ExitCode)"
}
Get-Content -LiteralPath (Join-Path $testDirectory 'logo-check-report.txt')
$arguments = '--smoke --logo-resume-check "{0}" --data-dir "{1}" --frames 360' -f $romPath,$testDirectory
$process = Start-Process -FilePath $executable -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "ロゴ設定の再起動検証失敗: $($process.ExitCode)" }
Get-Content -LiteralPath (Join-Path $testDirectory 'logo-resume-report.txt')
Write-Output "検証結果: $testDirectory"
