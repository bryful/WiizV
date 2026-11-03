param([string]$Java = 'C:\Program Files\Android\openjdk\jdk-21.0.8')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$folder = Join-Path (Split-Path $root -Parent) 'WiizV_signing'
$keystore = Join-Path $folder 'wiizv-release.jks'
$properties = Join-Path $folder 'release.properties'
$certificate = Join-Path $folder 'release-signing.cer'
if ((Test-Path -LiteralPath $keystore) -or (Test-Path -LiteralPath $properties) -or (Test-Path -LiteralPath $certificate)) {
    if ((Test-Path -LiteralPath $keystore) -and (Test-Path -LiteralPath $properties) -and (Test-Path -LiteralPath $certificate)) {
        Write-Output '既存のRelease署名鍵を維持します。'
        return
    }
    throw '署名ファイルが一部だけ存在します。新規生成せず、バックアップから復元してください。'
}
New-Item -ItemType Directory -Force $folder | Out-Null
# 秘密鍵とパスワードは現在のユーザーとSYSTEMのみ参照可能にする。
$acl = New-Object System.Security.AccessControl.DirectorySecurity
$acl.SetAccessRuleProtection($true, $false)
$userSid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
$systemSid = New-Object System.Security.Principal.SecurityIdentifier('S-1-5-18')
foreach ($sid in @($userSid, $systemSid)) {
    $rule = New-Object System.Security.AccessControl.FileSystemAccessRule($sid, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
    $acl.AddAccessRule($rule)
}
Set-Acl -LiteralPath $folder -AclObject $acl
$random = [Security.Cryptography.RandomNumberGenerator]::Create()
$bytes = New-Object byte[] 32
$random.GetBytes($bytes)
$random.Dispose()
$password = [BitConverter]::ToString($bytes).Replace('-', '')
$previousPassword = $env:WIIZV_SIGNING_PASSWORD
try {
    $env:WIIZV_SIGNING_PASSWORD = $password
    & (Join-Path $Java 'bin/keytool.exe') -genkeypair -noprompt -keystore $keystore -storetype JKS -alias wiizv-release -keyalg RSA -keysize 3072 -validity 10000 -dname 'CN=WiizV' -storepass:env WIIZV_SIGNING_PASSWORD -keypass:env WIIZV_SIGNING_PASSWORD
    if ($LASTEXITCODE -ne 0) { throw 'Release署名鍵の生成に失敗しました。' }
    [IO.File]::WriteAllText($properties, "storeFile=wiizv-release.jks`nstorePassword=$password`nkeyAlias=wiizv-release`nkeyPassword=$password`n", [Text.UTF8Encoding]::new($false))
    & (Join-Path $Java 'bin/keytool.exe') -exportcert -keystore $keystore -alias wiizv-release -storepass:env WIIZV_SIGNING_PASSWORD -file $certificate
    if ($LASTEXITCODE -ne 0) { throw '公開証明書の出力に失敗しました。' }
}
finally {
    $env:WIIZV_SIGNING_PASSWORD = $previousPassword
    $password = $null
    [Array]::Clear($bytes, 0, $bytes.Length)
}
Write-Output "署名鍵の保管先: $folder （配布しないでください）"
Write-Output '今後の更新に必要なため、このフォルダーを安全な場所へバックアップしてください。'
