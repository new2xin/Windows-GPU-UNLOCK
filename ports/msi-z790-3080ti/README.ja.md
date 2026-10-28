# MSI Z790 / Palit RTX 3080 Ti — 公開用ソース

## 検証済み構成

MSI PRO Z790-P WIFI DDR4 / BIOS 1.I0、Core i9-13900K、Palit RTX 3080 Ti 12 GB、PCI 10de:2208 / subsystem 1569:2208、VBIOS 94.02.71.80.52。Secure Bootは無効。Windows 10 Enterprise 19045、NVIDIA 591.74で検証しました。

## 第6版の変更

wrapperとchildでGPU・コンソール・入力・関連ドライバーの依存範囲を整合させました。判定前後の両グラフの関連辺を確認し、無関係なUSB列挙変化への過剰反応を抑えています。MSI childでは入力Resetを省き、Escによる取消しを維持しています。最大3回の読み直し、部分採取の破棄、失敗箇所の診断を追加しました。未知のGPU所有者、入力経路異常、必要な実機条件の不一致を許可する変更ではありません。

## 公開版と実機版の差

- C/Hの解除処理と安全検査は実機第6版のソースをそのまま収録しています。
- GPU UUID、USBシリアル、ボリュームGUID、Windows ESP GUIDを公開用のダミー値にしています。USB resolverとそのテストは同じダミー値を使います。実メディアへは一致しないため、対象確認が必要です。
- `build-experimental.ps1` と `build.ps1` のWindows ESP GUIDは必須引数です。公開版は実機用バイナリと同一ハッシュを生成する設定ではありません。
- 実機採取データ、PROM、NVIDIAアセット、v5の私有再現用バックアップは非同梱です。`make_*_fixture.py`、`test-port.ps1`、`test-experimental.ps1` の実機再生には対応データが必要です。`test-v5-reproduction.ps1` は採取側での再現手順を記録したものです。
- 内部ディスク縮小の計画スクリプトは公開対象から除外しました。実機でも実行していません。

## ファイルの役割

| ファイル | 役割 |
|---|---|
| machine.h | GPU、VBIOS、BIOS由来ドライバーのプロファイル条件 |
| build-experimental.ps1 | テストを通してwrapper/childを別ディレクトリへビルド |
| Resolve-UsbMedia.ps1 | ディスク番号や文字に依存しない媒体照合 |
| Stage-ExperimentalUsb.ps1 | バックアップ、入力・配置ハッシュ照合 |
| Enable-UsbAutomatic.ps1 | 実機成功後にUSB優先・Windows次点へ設定 |
| Restore-WindowsFirst.ps1 | Windows優先への復帰 |
| Benchmark-3080Ti.ps1 | 起動ログと測定状態を対応させる3回ベンチ |
| test_child_graph.h | 実機由来グラフ・変化に対するchild判定テスト |

## 実機版のビルド要件

上流と同様、clang/lld-link、EDK2ヘッダー、Python、MSVC/Windows SDK、正規に取得しハッシュ照合したNVIDIAアセットを使用します。`build-experimental.ps1 -WindowsEspGuid <自分の検証済みGUID> -Capture <自分の対応採取データ> -Assets <検証済みアセット>` の前に、プロファイルと媒体IDの照合が必要です。私有データを収録していないため、この公開スナップショットだけで実機再生テスト一式が完結するとは記載していません。

解除を毎回適用するには、FAT32 USBから完全電源断後に起動し、解除後にリセットせずWindowsへ引き継ぎます。Secure Boot無効などの前提と復帰経路を確認してください。画面の最終表示だけでは成功を判断できません。`attempted=1`、正常終了、後処理、Windows引継ぎをログで確認します。
