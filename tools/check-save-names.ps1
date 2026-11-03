param([Parameter(Mandatory=$true)][string]$Rom, [ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$stem = [IO.Path]::GetFileNameWithoutExtension($romPath)
$romBytes = [IO.File]::ReadAllBytes($romPath)
if ($romBytes.Length % 32768 -eq 512) { $romBytes = $romBytes[512..($romBytes.Length - 1)] }
$hasher = [Security.Cryptography.SHA256]::Create()
try { $romId = [BitConverter]::ToString($hasher.ComputeHash($romBytes)).Replace('-','').ToLowerInvariant() } finally { $hasher.Dispose() }
$directory = Join-Path $projectRoot ('verification\save-names-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$seed = Join-Path $directory 'seed'
$migrated = Join-Path $directory 'migrated'
$exe = Join-Path $projectRoot "x64\$Configuration\WiizV.exe"
New-Item -ItemType Directory -Force $seed,(Join-Path $migrated 'Saves'),(Join-Path $migrated "States\$romId") | Out-Null
function Invoke-Check([string]$Data, [string]$Check, [string]$RomFile = $romPath, [int]$ExpectedExit = 0) {
    $arguments = '--smoke {0} "{1}" --data-dir "{2}" --frames 900' -f $Check,$RomFile,$Data
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne $ExpectedExit) {
        Get-Content -LiteralPath (Join-Path $projectRoot "x64\$Configuration\last-error.txt")
        throw "保存名検証の終了コードが不正: $($process.ExitCode)"
    }
}
Invoke-Check $seed '--state-check'
$namedSave = Join-Path $seed "Saves\$stem.srm"
if (!(Test-Path -LiteralPath $namedSave)) { throw '通常セーブがROM名になっていません。' }
Copy-Item -LiteralPath $namedSave -Destination (Join-Path $migrated "Saves\$romId.srm")
for ($slot = 1; $slot -le 5; $slot++) {
    Copy-Item -LiteralPath (Join-Path $seed "States\$stem.slot$slot.wvs") -Destination (Join-Path $migrated "States\$romId\slot$slot.wvs")
    Copy-Item -LiteralPath (Join-Path $seed "expected-slot$slot.txt") -Destination $migrated
}
Copy-Item -LiteralPath (Join-Path $seed 'settings.ini') -Destination $migrated
$legacyFiles = @((Join-Path $migrated "Saves\$romId.srm")) + @(Get-ChildItem -LiteralPath (Join-Path $migrated "States\$romId") -File | ForEach-Object FullName)
$before = @($legacyFiles | ForEach-Object { (Get-FileHash -LiteralPath $_).Hash })
Invoke-Check $migrated '--state-resume-check'
$after = @($legacyFiles | ForEach-Object { (Get-FileHash -LiteralPath $_).Hash })
if (($before -join ',') -ne ($after -join ',')) { throw '移行時に旧ファイルが変更されました。' }
if (!(Test-Path -LiteralPath (Join-Path $migrated "Saves\$stem.srm"))) { throw '通常セーブが引き継がれませんでした。' }

# 新しい名前の保存先が存在する場合、旧スロット1で上書きしない。
$namedSlot = Join-Path $migrated "States\$stem.slot1.wvs"
Copy-Item -LiteralPath (Join-Path $migrated "States\$stem.slot2.wvs") -Destination $namedSlot -Force
$priorityHash = (Get-FileHash -LiteralPath $namedSlot).Hash
Invoke-Check $migrated ''
if ((Get-FileHash -LiteralPath $namedSlot).Hash -ne $priorityHash) { throw '既存の新しい名前のステートが上書きされました。' }

# 同名で内容が異なるROMを誤って通常セーブに結びつけない。
$other = Join-Path $directory 'other'
New-Item -ItemType Directory -Path $other | Out-Null
$differentRom = Join-Path $other ([IO.Path]::GetFileName($romPath))
$romBytes[0] = $romBytes[0] -bxor 1
[IO.File]::WriteAllBytes($differentRom, $romBytes)
$namedFiles = @(Get-ChildItem -LiteralPath (Join-Path $migrated 'Saves') -File) + @(Get-ChildItem -LiteralPath (Join-Path $migrated 'States') -File)
$beforeCollision = @($namedFiles | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName).Hash })
Invoke-Check $migrated '' $differentRom 1
$afterCollision = @($namedFiles | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName).Hash })
if (($beforeCollision -join ',') -ne ($afterCollision -join ',')) { throw '同名の別ROMで既存データが変わりました。' }
Write-Output "PASS: ROM名の通常セーブ/5スロット、旧名からのコピー移行、旧データ維持、新名優先、同名別ROMの保護 ($directory)"
