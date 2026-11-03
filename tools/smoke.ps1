param([Parameter(Mandatory=$true)][string]$Rom)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$testDirectory = Join-Path $projectRoot ('verification\smoke-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $testDirectory | Out-Null
$exe = Join-Path $projectRoot 'x64\Release\WiizV.exe'
function Invoke-Smoke {
    $arguments = '--smoke "{0}" --data-dir "{1}" --frames 900 --screenshot "{2}"' -f $romPath, $testDirectory, (Join-Path $testDirectory 'screen.bmp')
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru -Wait
    if ($process.ExitCode -ne 0) { throw "起動失敗: $($process.ExitCode)。実行ファイル横のlast-error.txtを確認してください。" }
}
Invoke-Smoke
$before = Get-ChildItem -LiteralPath (Join-Path $testDirectory 'Saves') -Filter '*.srm' | Get-FileHash
if (!$before) { throw '通常セーブが生成されませんでした。' }
$firstReport = Get-Content -LiteralPath (Join-Path $testDirectory 'smoke-report.txt') -Raw
Invoke-Smoke
$after = Get-ChildItem -LiteralPath (Join-Path $testDirectory 'Saves') -Filter '*.srm' | Get-FileHash
if ($before.Hash -ne $after.Hash) { throw '再起動で通常セーブの内容が変わりました。' }
$secondReport = Get-Content -LiteralPath (Join-Path $testDirectory 'smoke-report.txt') -Raw
if ($secondReport -notmatch 'sram_loaded=1') { throw '再起動時に通常セーブが読み込まれませんでした。' }
if (($firstReport -replace 'sram_loaded=0', 'sram_loaded=1') -ne $secondReport) { throw '再起動後の映像・音声出力の集計が一致しません。' }
Write-Output "PASS: 起動・映像・非無音の音声・通常セーブ再読込 ($testDirectory)"
Write-Output $secondReport
