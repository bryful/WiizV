# WiizV

<img src="wiizv-icon.png" alt="WiizV" width="160">

SFC版Wizardry V向けの非公式プレイヤーの配布リポジトリーです。
Windows x64／Android 6以降のARM64に対応します。Android APKはx86_64も含みます。
ROM・ゲーム画像・音楽・セーブデータ・署名用の秘密鍵は付属しません。
所有する対応ROM（.smc／.sfc）を用意してください。

## ダウンロードと起動

リポジトリーの **Code → Download ZIP** から全体を取得して展開してください。

- **Windows**：`Windows/WiizV.exe`を起動します。DLL・licensesも一緒に配置してください。
  必要なMicrosoft Visual C++ v14ランタイム（x64）が未導入の場合は、
  [Microsoft公式案内](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/)から導入してください。
- **Android**：[WiizV-0.2.4-release.apk](Android/WiizV-0.2.4-release.apk)を端末へコピーしてインストールします。
  起動後にDocumentsまたはDocuments/WiizVを選び、保存先へのアクセスを許可してROMを選択します。
  旧debug署名版から初めて移行する場合は上書きできません。
  保存フォルダー全体を別の場所へ控え、旧アプリをアンインストールしてから導入し、同じ保存フォルダーを選び直してください。

## 主な機能

- 5スロットのステート保存・読込、通常セーブ。
- BGMのテンポとSEの再生速度を保つ1～4倍速。
- 全階層マップからの移動。移動前はスロット1へ保存します。
- 常時効果・MP維持、ABXY交換、CRT表示。
- キャラクターのGOLD・経験値・属性変更、全スペル習得。
- 画面上のパッド、キーボード、SDL対応ゲームパッドによる操作。

## 説明書

- [詳しい使用説明書](使用説明書.md)
- [Androidのインストール手順](Android/インストール.md)
- `使用説明書.html`はダウンロードしてブラウザーで開いてください。
- `SHA256SUMS.txt`に配布ファイルのSHA-256を収録しています。

## ライセンス・公開範囲

この公開リポジトリーは配布物・説明書・第三者のライセンスと必要なコアのソースを収録します。
開発用リポジトリーの公開を意味するものではありません。
同梱ソフトの条件はそれぞれの著作権表示・ライセンスに従ってください。
Snes9xには**非商用利用・配布の条件**があります。この一式をMIT等の単一ライセンスとして扱わないでください。

詳細は[第三者ソフトウェアについて](THIRD_PARTY_NOTICES.md)と`Windows/licenses`、APK内の`assets/licenses`を参照してください。
ゲーム本体やその権利は付属せず、各権利者に帰属します。

Windowsの動作検証、Android x86_64エミュレーターでの検証を行っています。
すべての実機・ゲームパッド・場面での動作を保証するものではありません。
