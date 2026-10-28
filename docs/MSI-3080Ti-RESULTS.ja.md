# MSI RTX 3080 Ti 第6版 — 実機結果

**自動起動に関する訂正（2026-10-28）:** BIOSのBoot OverrideからKIOXIAを手動選択する必要があると判明しました。実解除・Windows引継ぎ・ベンチ結果は有効ですが、無操作の自動起動は未確認です。15:12に、USBの媒体IDと `\EFI\BOOT\BOOTX64.EFI` を明示する新しい起動項目を先頭へ設定しました。修正後の電源投入テストは確認待ちです。

2026-10-27 13:44:08.500 JSTと2026-10-28 10:35:34.500 JSTの2回、実解除、後処理、Windows起動を確認しました。

```text
pre-FLR selectors: ss0=0x88888888 expected=0x88888888 ss1=0x8 expected=0x8
transaction result=0 containment=0 started=1 quiesced=1 fault=0 retained=0 maps=0 FLR=1
FINAL status=0 stage=transaction attempted=1 driver_release=0 clean=1
```

childのdriver_release=0は、画面ドライバーの切離しをwrapperが担当した意味です。解除はattempted=1で実行されています。entry SS0は2回で0x11515124と0x10515124に変わりましたが、それぞれの起動値を確認して処理し、FLR後も目標SS0/SS1を確認しています。

## 性能

| 演算 | 未解除 TFLOPS | 解除後 TFLOPS | 変化 |
|---|---:|---:|---:|
| FP32 pedantic | 25.587367 | 25.575025 | -0.048% |
| FP16 Tensor / FP32累積 | 73.498655 | 124.506248 | +69.399% |
| BF16 Tensor / FP32累積 | 72.491957 | 124.506248 | +71.752% |

2026-10-27測定。各3回の試行中央値を、さらに中央値で集約しています。n=4096、各精度30標本/試行、ウォームアップ2秒。全要素1の行列積について、先頭・末尾が4096であることを確認しました。

GPU、NVIDIA 591.74、CUDA 12.6 / cuBLAS 12.6.3、ソース・コンパイラー・cuBLAS DLLのSHA-256は一致。電力上限は269.50 Wで変更していません。温度と動的クロックは固定していません。FP32はほぼ同じで、Tensor演算のFP16/BF16が1.69〜1.72倍になりました。

## 配置した実機版

- wrapper 42,496 bytes: `1fdf5c129a81c8dd0056ff8b13f043efbd649ca1909f6bab6c36b2011b9c37e0`
- child 68,096 bytes: `cb77924d22a8b32bd4e752010c84b715366854a08028447f101eb69b757a3b90`

実機版バイナリは同梱していません。公開版の配備設定は個体IDを伏せ、Windows ESP GUIDを必須引数にしたため、バイナリの同一ハッシュ再生成を主張していません。

## 原データと確認範囲

[ベンチ比較](../evidence/msi-z790-3080ti/benchmark-comparison.json)、[起動のレビュー](../evidence/msi-z790-3080ti/reviewed-boots.json)、stock/unlockedの各3試行JSONを同梱しています。raw capture全文は非同梱で、元ログのSHA-256と要点を記録しています。

ビルド時のHardwareUnlockTested=falseや、採取・測定ツールの固定した未レビューフラグは後の実機判定を自動更新するものではありません。公開結果は実機ログと測定条件を照合したレビューに基づきます。

ホスト側では、core 112ケース、実機グラフ17シナリオと765変更、release 5,274チェック、wrapper 1,572チェック、3プロファイルそれぞれcold 180/flow 39ケース、媒体照合28チェックを通しています。これらはEFI/GPUを模擬した検証で、実機起動2回とは区別しています。私有の採取データが必要な再生テストを公開スナップショットだけで再実行できるとはしていません。

ゲーム、VRChat、画像生成・音声生成のアプリ全体の速度、長時間安定性、BIOS/GPU変更後の互換性、USBを抜いた起動経路は未検証です。
