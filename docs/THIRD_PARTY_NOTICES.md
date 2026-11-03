# 第三者ソフトウェアと配布条件

この文書は同梱ソフトの所在を案内するものです。各原文ライセンスが優先します。

| 構成要素 | 利用・配布条件の所在 |
|---|---|
| Snes9x／libretroコアとWiizVによる変更 | `Windows/licenses/snes9x/LICENSE`。非商用の使用・複製・変更・配布を許可し、著作権表示とライセンスの保持を求めています。商用利用を許可する表示ではありません。 |
| Snes9x内の別ライセンスの部品 | `Windows/licenses/snes9x`の各LICENSE、同梱`source.zip`内の個別ファイル。NTSCフィルター等のLGPL条項も保持しています。 |
| SDL3 | Windowsは3.4.4、Androidは3.2.28。zlibライセンス。各`SDL3.txt`に原文を収録しています。 |
| libretro APIヘッダー | `libretro.h`内のMIT著作権・許諾表示。 |
| Android libc++／NDKランタイム | APKの`assets/licenses/NDK/`にNDKとLLVMのNOTICEを収録。Apache 2.0 with LLVM Exceptions等、各構成部分の条件を参照してください。 |

## ソースと変更の確認

Snes9xの元ソースは`Windows/licenses/snes9x/source.zip`およびAPKの同じ名前のライセンスフォルダーに収録しています。
WiizVのコア拡張は`wiizv_*.inc`・`wiizv_*.h`、Windows用の適用・ビルドスクリプトは`prepare-core.ps1`・`build-core.ps1`です。
変更内容・元ソースのコミット・入手先は`Windows/licenses/PROVENANCE.md`を参照してください。
同梱ライセンス、原ソース、変更ファイルを再配布時も保持してください。

公式資料：

- [Snes9xソース](https://github.com/snes9xgit/snes9x/)
- [SDL3 Windows版ライセンス](https://github.com/libsdl-org/SDL/blob/release-3.4.4/LICENSE.txt)
- [SDL3 Android版ライセンス](https://github.com/libsdl-org/SDL/blob/release-3.2.28/LICENSE.txt)
- [LLVMライセンス](https://llvm.org/LICENSE.txt)

ゲームのROM、ゲームから抽出した素材、ゲームマニュアル、ユーザーのセーブ、認証情報・秘密鍵は配布対象に含めていません。
アイコンは依頼者のWiizアイコンを参考に画像生成ツールで作成したカートリッジの図案です。

## ソースリポジトリ内の所在

WiizV 独自部分はルートの `LICENSE`（MIT）を参照してください。
Snes9x の原文ライセンス・元ソース・変更部分は `third_party/snes9x/`、SDL3 は `third_party/SDL3/`、入手先とバージョンは `third_party/PROVENANCE.md` にあります。Android の SDL Java コードと Gradle wrapper は `android/PROVENANCE.md` に記載された元の条件に従います。上記の `Windows/licenses/` は配布パッケージ内のパスです。
