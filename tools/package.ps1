$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$releaseApk = Join-Path $root 'android/app/build/outputs/apk/release/app-release.apk'
& (Join-Path $PSScriptRoot 'verify-android-release.ps1') -Apk $releaseApk
$destination = Join-Path (Split-Path $root -Parent) 'WiizV_app'
$windows = Join-Path $destination 'Windows'
$android = Join-Path $destination 'Android'
New-Item -ItemType Directory -Force $windows, $android, (Join-Path $windows 'Rom') | Out-Null
$otherApks = @(Get-ChildItem -LiteralPath $android -Filter '*.apk' | Where-Object Name -ne 'WiizV-0.2.4-release.apk')
if ($otherApks.Count) { throw '配布先に旧APKが残っています。配布先の外へ保管してから再実行してください。' }
foreach ($name in @('WiizV.exe', 'SDL3.dll', 'wiizv_snes9x.dll')) {
    Copy-Item -LiteralPath (Join-Path $root "x64/Release/$name") -Destination $windows
}
$licenses = Join-Path $windows 'licenses'
New-Item -ItemType Directory -Force $licenses | Out-Null
Copy-Item -Path (Join-Path $root 'x64/Release/licenses/*') -Destination $licenses -Recurse -Force
Copy-Item -LiteralPath $releaseApk -Destination (Join-Path $android 'WiizV-0.2.4-release.apk')
Copy-Item -LiteralPath (Join-Path $root 'android/INSTALL-ja.md') -Destination (Join-Path $android 'インストール.md')
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination (Join-Path $destination '使用説明書.md')
Copy-Item -LiteralPath (Join-Path $root 'docs/使用説明書.html') -Destination $destination
Copy-Item -LiteralPath (Join-Path $root 'assets/wiizv-icon.png') -Destination $destination
Copy-Item -LiteralPath (Join-Path $root 'docs/はじめに.txt') -Destination $destination
Copy-Item -LiteralPath (Join-Path $root 'docs/DISTRIBUTION-README.md') -Destination (Join-Path $destination 'README.md')
Copy-Item -LiteralPath (Join-Path $root 'docs/THIRD_PARTY_NOTICES.md') -Destination $destination
$distributionIgnore = @('.git/', '*.jks', '*.keystore', '*.p12', '*.pfx', '*.key', '*.pem', 'release.properties', 'signing/', '.env', '.env.*', '*.nes', '*.smc', '*.sfc', '*.srm', '*.sav', '*.wvs', '*.state', '*.pdb', '*.ilk', '*.log', '*.tmp')
[IO.File]::WriteAllLines((Join-Path $destination '.gitignore'), $distributionIgnore, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $destination '.gitattributes'), "* -text`n", [Text.UTF8Encoding]::new($false))
$checksums = Get-ChildItem -LiteralPath $destination -Recurse -File -Force | Where-Object { $_.Name -ne 'SHA256SUMS.txt' -and $_.FullName -notlike (Join-Path $destination '.git/*') } | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($destination.Length + 1).Replace('\', '/')
    (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $relative
}
[IO.File]::WriteAllLines((Join-Path $destination 'SHA256SUMS.txt'), [string[]]$checksums, [Text.UTF8Encoding]::new($false))
$archive = Join-Path (Split-Path $destination -Parent) 'WiizV_app.zip'
# 公開後もGitの管理情報・履歴を配布ZIPへ含めない。
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archiveStream = [IO.File]::Create($archive)
$zip = [IO.Compression.ZipArchive]::new($archiveStream, [IO.Compression.ZipArchiveMode]::Create)
try {
    Get-ChildItem -LiteralPath $destination -Recurse -File -Force | Where-Object { $_.FullName -notlike (Join-Path $destination '.git/*') } | ForEach-Object {
        $relative = $_.FullName.Substring($destination.Length + 1).Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, "WiizV_app/$relative") | Out-Null
    }
    $zip.CreateEntry('WiizV_app/Windows/Rom/') | Out-Null
}
finally { $zip.Dispose(); $archiveStream.Dispose() }
Write-Output $destination
Write-Output $archive
