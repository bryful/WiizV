# Android Release署名

WiizVのAPKを専用のRelease鍵で署名します。鍵とパスワードはソースや配布物へ含めません。

## 初回作成と保管

保管先はWiizVと同じ階層の `WiizV_signing` です。Gradle・初期化・検証スクリプトはすべてここを参照します。

初回だけ `powershell -File tools/initialize-android-signing.ps1` を実行します。
既存の鍵がある場合は維持し、署名ファイルの一部が欠けている場合も再生成せず停止します。

- 秘密鍵：`../WiizV_signing/wiizv-release.jks`
- ローカル設定：`../WiizV_signing/release.properties`（鍵のパス・alias・パスワード）
- 公開証明書：`../WiizV_signing/release-signing.cer`（秘密情報を含まない公開証明書）

`../WiizV_signing` は現在のWindowsユーザーとSYSTEMだけがアクセスでき、リポジトリーの外に置きます。
このフォルダー全体を、安全な別の場所へバックアップしてください。更新版の署名には同じ鍵が必要です。
PCを変更するときは秘密鍵と設定を一緒に復元し、公開証明書も同じものを復元します。
秘密鍵やパスワードを配布用フォルダー・ZIP・公開リポジトリーへコピーしないでください。

## ビルド・検証・配布

```powershell
powershell -File tools/build-android.ps1 -Configuration Release -Offline
powershell -File tools/package.ps1
```

Release APKは `android/app/build/outputs/apk/release/app-release.apk` です。
ビルドの完了時とパッケージ作成前に `tools/verify-android-release.ps1` で署名を検証します。
公開証明書のSHA-256と署名者証明書が一致し、パッケージ名が正しく、非debuggableであることを確認します。
署名設定がない場合はReleaseビルドを停止します。Debugビルドは従来どおり利用できます。

配布先はリポジトリーと同じ階層の `../WiizV_app` です。
公開証明書のSHA-256は次のコマンドで確認できます。パスワードは不要です。

```powershell
Get-FileHash ../WiizV_signing/release-signing.cer -Algorithm SHA256
```

## 旧debug版からの移行

署名が異なるため、旧debug版への上書きはできません。
Documents/WiizV全体を別の場所へコピーして控え、旧debug版をアンインストールしてからRelease版を導入します。
Documents/WiizV自体は削除せず、起動後に同じ保存フォルダーとROMを選び直します。
同じRelease鍵で署名した今後の更新版は上書きインストールできます。

Android公式資料：[Sign your app](https://developer.android.com/studio/publish/app-signing)
