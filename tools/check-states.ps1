param([Parameter(Mandatory=$true)][string]$Rom, [ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$testDirectory = Join-Path $projectRoot ('verification\states-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $testDirectory | Out-Null
$exe = Join-Path $projectRoot "x64\$Configuration\WiizV.exe"
foreach ($check in @('--state-check', '--state-resume-check')) {
    $arguments = '--smoke {0} "{1}" --data-dir "{2}" --frames 900' -f $check,$romPath,$testDirectory
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        $errorPath = Join-Path $projectRoot "x64\$Configuration\last-error.txt"
        if (Test-Path -LiteralPath $errorPath) { Get-Content -LiteralPath $errorPath | Write-Output }
        throw "ステート検証失敗: $($process.ExitCode)"
    }
}
Get-Content -LiteralPath (Join-Path $testDirectory 'state-check-report.txt')
Get-Content -LiteralPath (Join-Path $testDirectory 'state-resume-report.txt')
Write-Output "検証結果: $testDirectory"
