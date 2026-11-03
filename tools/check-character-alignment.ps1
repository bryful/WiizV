param([Parameter(Mandatory=$true)][string]$Rom, [ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$testDirectory = Join-Path $projectRoot ('verification\character-alignment-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $testDirectory | Out-Null
$executable = Join-Path $projectRoot "x64\$Configuration\WiizV.exe"
$arguments = '--smoke --character-alignment-check "{0}" --data-dir "{1}" --frames 900' -f $romPath,$testDirectory
$process = Start-Process -FilePath $executable -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) {
    Get-Content -LiteralPath (Join-Path $projectRoot "x64\$Configuration\last-error.txt")
    throw "属性変更検証失敗: $($process.ExitCode)"
}
Get-Content -LiteralPath (Join-Path $testDirectory 'character-alignment-check-report.txt')
$arguments = '--smoke --character-alignment-resume-check "{0}" --data-dir "{1}" --frames 900' -f $romPath,$testDirectory
$process = Start-Process -FilePath $executable -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "属性変更の通常セーブ再起動検証失敗: $($process.ExitCode)" }
Get-Content -LiteralPath (Join-Path $testDirectory 'character-alignment-resume-report.txt')
Write-Output "検証結果: $testDirectory"
