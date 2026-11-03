param(
    [Parameter(Mandatory=$true)][string]$Apk,
    [string]$Sdk = $(if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android/Sdk' }),
    [string]$Java = $env:JAVA_HOME
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$env:JAVA_HOME = $Java
$buildTools = Get-ChildItem -LiteralPath (Join-Path $Sdk 'build-tools') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (!$buildTools) { throw 'Android build-toolsが見つかりません。' }
$report = & (Join-Path $buildTools.FullName 'apksigner.bat') verify --verbose --print-certs $Apk
if ($LASTEXITCODE -ne 0) { throw 'APK署名の検証に失敗しました。' }
$digests = @([regex]::Matches(($report -join "`n"), 'certificate SHA-256 digest: ([0-9a-fA-F]{64})') | ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Sort-Object -Unique)
$expected = (Get-FileHash -LiteralPath (Join-Path (Split-Path $root -Parent) 'WiizV_signing/release-signing.cer') -Algorithm SHA256).Hash
if ($digests.Count -ne 1 -or $digests[0] -ne $expected) { throw '専用Release証明書とAPKの署名が一致しません。' }
$badging = & (Join-Path $buildTools.FullName 'aapt.exe') dump badging $Apk
if ($LASTEXITCODE -ne 0) { throw 'APK情報の検証に失敗しました。' }
if ($badging -match '^application-debuggable' -or !($badging -match "^package: name='com.wiizv.app'")) { throw '配布対象ではないAPKです。' }
Write-Output ($report | Select-String 'Verified using|certificate SHA-256')
Write-Output 'PASS: 専用Release署名・非debuggable APK'
