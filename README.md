# Windows GPU UNLOCK — MSI Z790 / RTX 3080 Ti 移植版

**自動起動に関する訂正（2026-10-28）:** BIOSのBoot OverrideからKIOXIAを手動選択する必要があると判明しました。実解除・Windows引継ぎ・ベンチ結果は有効ですが、無操作の自動起動は未確認です。15:12に、USBの媒体IDと `\EFI\BOOT\BOOTX64.EFI` を明示する新しい起動項目を先頭へ設定しました。修正後の電源投入テストは確認待ちです。

Palit RTX 3080 TiをMSI PRO Z790-P WIFI DDR4（BIOS 1.I0）で検証した、実験的なUSB UEFI移植版です。2026年10月27日・28日の2回、実解除・正常な後処理・Windowsへの直接引継ぎを確認しました。

| 演算 | 未解除 TFLOPS | 解除後 TFLOPS | 変化 |
|---|---:|---:|---:|
| FP32 pedantic | 25.587 | 25.575 | -0.05% |
| FP16 Tensor / FP32累積 | 73.499 | 124.506 | +69.40% |
| BF16 Tensor / FP32累積 | 72.492 | 124.506 | +71.75% |

各3回の中央値。同じGPU、ドライバー591.74、CUDA 12.6/cuBLAS 12.6.3、電力上限269.50 Wです。短時間GEMMの結果で、ゲーム・VRChat・AIアプリ全体の速度や長時間安定性は未測定です。

## 内容

- [実機検証と測定方法](docs/MSI-3080Ti-RESULTS.ja.md)
- [移植版ソース・設定の前提](ports/msi-z790-3080ti/README.ja.md)
- [機械可読の検証記録](evidence/msi-z790-3080ti/)
- [元プロジェクト](https://github.com/impacifist/Windows-GPU-UNLOCK)
- [元プロジェクトのREADME](docs/UPSTREAM-README.en.md)

## 利用前提

このプロファイルは上記の実機・BIOS・GPU/VBIOS・UEFIドライバーの一致を前提とします。同じ3080 Tiという製品名だけで互換性は判断できません。GPU/表示/入力の依存関係、PCI/PROM/cold状態の検査は維持しています。

公開版には解除のC/Hソースとビルド・検証スクリプトを収録しています。個体固有のIDはダミー値に置換し、実機用EFI、NVIDIAファームウェア、PROMダンプ、NVRAM/BCDバックアップ、私有の採取ログは収録していません。実機用ビルドには自分の採取データ、検証済みアセット、Windows ESP GUIDが必要です。元の実機用ファイルは別に保持しています。

USBから毎回適用し、解除後はリセットを挟まずWindowsを直接起動する方式です。カードの恒久書換えや汎用の導入ツールとして配布するものではありません。

## 来歴とライセンス

impacifist/Windows-GPU-UNLOCKのローカル基点 `849f996` にMSI移植の変更を重ねた公開用ソーススナップショットです。元の履歴を複製したGitHub forkではありません。上流の著作権表示、GPL-2.0、第三者ライセンスを保持しています。新しいMSI移植の実機結果は本リポジトリの記録を参照してください。上流のrelease/EFIは上流由来であり、今回のMSI用EFIではありません。
