param([string]$Sdk = $(if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android/Sdk' }), [string]$Java = $env:JAVA_HOME, [switch]$Offline, [ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'prepare-core.ps1')
$sdlArchive = Join-Path $projectRoot 'third_party/SDL3/android-source.zip'
if ((Get-FileHash -LiteralPath $sdlArchive).Hash -ne '24A30069AF514A6C6B773BDC8CCCA8B321661B251381ACC1DAEEBF8C8F4A109A') { throw 'Android用SDLソースのハッシュが一致しません。' }
if (!(Test-Path -LiteralPath (Join-Path $projectRoot 'third_party/SDL3/SDL3-3.2.28/CMakeLists.txt'))) { Expand-Archive -LiteralPath $sdlArchive -DestinationPath (Join-Path $projectRoot 'third_party/SDL3') }
$androidProject = Join-Path $projectRoot 'android'
$assetRoot = Join-Path $androidProject 'app/src/main/assets/licenses'
$ndkRoot = Join-Path $Sdk 'ndk/30.0.16248370'
$ndkNotices = Join-Path $assetRoot 'NDK'
New-Item -ItemType Directory -Force $ndkNotices | Out-Null
Copy-Item -LiteralPath (Join-Path $ndkRoot 'NOTICE') -Destination $ndkNotices
Copy-Item -LiteralPath (Join-Path $ndkRoot 'NOTICE.toolchain') -Destination $ndkNotices
Copy-Item -LiteralPath (Join-Path $ndkRoot 'toolchains/llvm/prebuilt/windows-x86_64/NOTICE') -Destination (Join-Path $ndkNotices 'LLVM-NOTICE.txt')
New-Item -ItemType Directory -Force (Join-Path $assetRoot 'snes9x') | Out-Null
Copy-Item -Path (Join-Path $projectRoot 'third_party/snes9x/*LICENSE*') -Destination (Join-Path $assetRoot 'snes9x')
foreach ($pattern in @('*.inc','wiizv_*.h','source.zip','libretro.h')) { Copy-Item -Path (Join-Path $projectRoot "third_party/snes9x/$pattern") -Destination (Join-Path $assetRoot 'snes9x') }
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/SDL3/SDL3-3.2.28/LICENSE.txt') -Destination (Join-Path $assetRoot 'SDL3.txt')
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/PROVENANCE.md') -Destination (Join-Path $assetRoot 'PROVENANCE.md')
[IO.File]::WriteAllText((Join-Path $androidProject 'local.properties'), ('sdk.dir=' + $Sdk.Replace('\','/') + "`n"), [Text.UTF8Encoding]::new($false))
$env:JAVA_HOME = $Java
$env:ANDROID_HOME = $Sdk
$env:ANDROID_SDK_ROOT = $Sdk
$env:GRADLE_USER_HOME = Join-Path $projectRoot 'verification/android-gradle'
# Gradle wrapper uses the configured local dependency cache.
$gradle = Join-Path $androidProject 'gradlew.bat'
if (!(Test-Path -LiteralPath $gradle)) { $gradle = Join-Path $androidProject 'gradlew.bat' }
$taskUserHome = Join-Path $projectRoot 'verification/android-user'
$buildArguments = @('-p', $androidProject, "assemble$Configuration", '--no-daemon', '--console=plain', "-Duser.home=$taskUserHome")
if ($Offline) { $buildArguments += '--offline' }
& $gradle @buildArguments
if ($LASTEXITCODE -ne 0) { throw "Androidビルド失敗: $LASTEXITCODE" }
$variant = $Configuration.ToLowerInvariant()
$apk = Join-Path $androidProject "app/build/outputs/apk/$variant/app-$variant.apk"
if ($Configuration -eq 'Release') { & (Join-Path $PSScriptRoot 'verify-android-release.ps1') -Apk $apk -Sdk $Sdk -Java $Java }
Write-Output $apk
