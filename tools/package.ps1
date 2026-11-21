param([string]$Destination, [string]$Archive)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gradle = Get-Content -LiteralPath (Join-Path $root 'android/app/build.gradle') -Raw
$version = [regex]::Match($gradle, "versionName '([0-9]+\.[0-9]+\.[0-9]+)'").Groups[1].Value
if (!$version) { throw 'AndroidのversionNameを取得できません。' }
$apkName = "WiizV-$version-release.apk"
$releaseApk = Join-Path $root 'android/app/build/outputs/apk/release/app-release.apk'
& (Join-Path $PSScriptRoot 'verify-android-release.ps1') -Apk $releaseApk
if (!$Destination) { $Destination = Join-Path (Split-Path $root -Parent) 'WiizV_app' }
$distributionRoot = [IO.Path]::GetFullPath($Destination)
$destination = Join-Path $distributionRoot $version
$windows = Join-Path $destination 'Windows'
$android = Join-Path $destination 'Android'
New-Item -ItemType Directory -Force $windows, $android, (Join-Path $windows 'Rom') | Out-Null
$otherApks = @(Get-ChildItem -LiteralPath $android -Filter '*.apk' | Where-Object Name -ne $apkName)
if ($otherApks.Count) { throw '配布先に旧APKが残っています。配布先の外へ保管してから再実行してください。' }
foreach ($name in @('WiizV.exe', 'SDL3.dll', 'wiizv_snes9x.dll')) {
    Copy-Item -LiteralPath (Join-Path $root "x64/Release/$name") -Destination $windows
}
$licenses = Join-Path $windows 'licenses'
New-Item -ItemType Directory -Force $licenses | Out-Null
Copy-Item -Path (Join-Path $root 'x64/Release/licenses/*') -Destination $licenses -Recurse -Force
Copy-Item -LiteralPath $releaseApk -Destination (Join-Path $android $apkName)
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
# リポジトリ直下は案内、実行ファイル・資料はバージョン別に配置する。
$index = "# WiizV 配布版`n`n最新版: [$version]($version/README.md)`n`n- [Windows版]($version/Windows/)`n- [Android APK]($version/Android/$apkName)`n- [使用説明書]($version/使用説明書.html)`n`n各バージョンのフォルダーにアプリ・ライセンス・使用説明書・SHA256SUMS.txtをまとめています。ROMは付属しません。`n"
[IO.File]::WriteAllText((Join-Path $distributionRoot 'README.md'), $index, [Text.UTF8Encoding]::new($false))
$checksums = Get-ChildItem -LiteralPath $destination -Recurse -File -Force | Where-Object { $_.Name -ne 'SHA256SUMS.txt' -and $_.FullName -notlike (Join-Path $destination '.git/*') } | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($destination.Length + 1).Replace('\', '/')
    (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $relative
}
[IO.File]::WriteAllLines((Join-Path $destination 'SHA256SUMS.txt'), [string[]]$checksums, [Text.UTF8Encoding]::new($false))
if (!$Archive) { $Archive = Join-Path $root "verification/WiizV-$version.zip" }
New-Item -ItemType Directory -Force (Split-Path ([IO.Path]::GetFullPath($Archive)) -Parent) | Out-Null
$archive = [IO.Path]::GetFullPath($Archive)
# 公開後もGitの管理情報・履歴を配布ZIPへ含めない。
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archiveStream = [IO.File]::Create($archive)
$zip = [IO.Compression.ZipArchive]::new($archiveStream, [IO.Compression.ZipArchiveMode]::Create)
try {
    Get-ChildItem -LiteralPath $destination -Recurse -File -Force | Where-Object { $_.FullName -notlike (Join-Path $destination '.git/*') } | ForEach-Object {
        $relative = $_.FullName.Substring($destination.Length + 1).Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, "WiizV_app/$version/$relative") | Out-Null
    }
    $zip.CreateEntry("WiizV_app/$version/Windows/Rom/") | Out-Null
}
finally { $zip.Dispose(); $archiveStream.Dispose() }
Write-Output $destination
Write-Output $archive
