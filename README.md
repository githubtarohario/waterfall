# 【SPH法】水しぶきシミュレーション / Water Splash Simulation (DirectX 11)

動画『【SPH法】水しぶきシミュレーション』と同じシーン ―
**2 つの水塊が透明な水槽へ落下し、盛大な飛沫を上げて収まる** ― を
GPU 上の **SPH 法 (WCSPH)** と **マーチングキューブ法** で再現する DirectX 11 アプリケーションです。

```
E:\work\水しぶき
├─ build.bat            MSVC ビルドスクリプト (build\SPHSplash.exe を生成)
├─ CMakeLists.txt       CMake でビルドする場合
├─ docs\               API 仕様書 (Markdown / HTML / PDF)
├─ book\               解説書『GPU で作る水しぶき』(CG解説書.pdf) とその生成スクリプト
├─ src\
│   ├─ main.cpp          Win32 ウィンドウ / メインループ / 入力 / フレーム録画
│   ├─ SimConfig.h       全パラメータ (シーン配置・SPH 定数・カメラ・ライト)
│   ├─ D3DUtil.*         DirectX 11 ユーティリティ (デバイス・バッファ・シェーダー)
│   ├─ SPHSimulator.*    GPU WCSPH (一様格子 + カウンティングソート)
│   ├─ MarchingCubes.*   GPU マーチングキューブ (スカラー場 → 三角形バッファ)
│   ├─ MCTables.*        MC 三角形テーブルのアルゴリズム生成と自己検証
│   ├─ Renderer.*        シャドウ / 床 / 水面 / ガラス / ポストプロセス
│   └─ mc_selftest.cpp   テーブル生成の単体テスト (コンソール)
└─ shaders\
    ├─ sph_common.hlsli  SPH 定数バッファ・格子・カーネル
    ├─ sph.hlsl          SPH の 7 つの計算シェーダー
    ├─ mc.hlsl           スカラー場 / 平滑化 / マーチングキューブ / 描画引数
    ├─ render_common.hlsli  描画用定数バッファ・環境光・シャドウ
    ├─ water.hlsl        水面メッシュの描画 (反射・屈折・吸収・ハイライト)
    ├─ scene.hlsl        床とガラス水槽
    └─ post.hlsl         SSAA 縮小・トーンマップ・周辺減光
```

## ビルド

Visual Studio 2022 (または 18) の C++ ツールが必要です。

```
build.bat          リリースビルド
build.bat debug    デバッグビルド
```

`build\SPHSplash.exe` と `build\mc_selftest.exe` が生成されます。
シェーダーは実行時に `shaders\` から読み込まれます (exe と同じ階層、またはその 1 つ上を自動で探します)。

## 実行

```
build\SPHSplash.exe                     リアルタイム表示 (1 描画フレーム = 動画の 1/24 秒)
build\SPHSplash.exe --record            capture\frame_XXXX.bmp に 24fps 相当で連番保存
build\SPHSplash.exe --record out --frames 102   102 フレーム (4.25 秒) 保存して終了
build\SPHSplash.exe --spacing 0.011     粒子間隔を細かくして高精細に (粒子数 ≒ 2 倍)
build\SPHSplash.exe --particles         粒子を点で表示するデバッグモード
build\SPHSplash.exe --particles --nowater   粒子だけを表示 (水面メッシュを描かない)
build\SPHSplash.exe --resttest          水塊を床に静置する安定性テスト
```

| キー | 動作 |
|---|---|
| Space | 一時停止 / 再開 |
| S | 1 フレーム進める |
| R | リセット |
| P | 粒子表示の切り替え |
| W | 水面メッシュ表示の切り替え |
| G | ガラス水槽表示の切り替え |
| 右ドラッグ / ホイール | カメラ回転 / ズーム |
| Esc | 終了 |

連番 BMP は例えば ffmpeg で動画にできます:
`ffmpeg -framerate 24 -i capture/frame_%04d.bmp -pix_fmt yuv420p splash.mp4`

## 手法の概要

### SPH 法 (shaders/sph.hlsl)
1 サブステップごとに以下の計算シェーダーを順に実行します (RTX 3060 で 1 フレーム ≒ 70ms)。

1. **格子構築** … 粒子の所属セルをアトミックにカウント → 単一グループの累積和 → セル順に並べ替え
2. **密度・圧力** … poly6 カーネルで密度、Tait 方程式 `p = B((ρ/ρ0)^7 − 1)` で圧力 (負圧は 0)。
   壁の近傍では欠損する近傍分を解析的に補う「ゴースト密度」を加算
3. **力** … spiky カーネルの圧力項 (Monaghan 対称形)、粘性項、**Monaghan 人工粘性** (衝撃時の飛散を抑制)、XSPH
4. **積分** … シンプレクティック・オイラー、水槽の壁 / 床 / 領域との衝突

時間刻みは CFL 条件 `dt = 0.4 h / c` からフレーム (1/24 秒) を等分割して自動決定します。

動画の水槽は落下速度から幅 ≒ 2 m と推定されます。粒子数を抑えるため幾何は 1 m 角で作り、
**フルード相似** (t ∝ √(L/g)) により重力を 1/2 にして同じ時間経過を再現しています
(`SimConfig.h` の `SCENE_SCALE`)。

### マーチングキューブ法 (shaders/mc.hlsl, src/MCTables.cpp)
- 各格子点でスカラー場 `F(x) = Σ (1 − |x − x_j|²/R²)³` を粒子から計算し、7 点フィルタで平滑化
- 256 通りの三角形テーブルは**起動時にアルゴリズム生成**します: 交差エッジを面の周りで辿って閉ループを作り、
  曖昧面は「内側頂点を分離する」規則で統一するので隣接セル間で必ず整合し、穴のないメッシュになります。
  起動時に球で自己検証 (閉多様体・外向き) を行います
- セルごとに 1 スレッドで三角形を `AppendStructuredBuffer` へ追加し、`DrawInstancedIndirect` で描画 (CPU 往復なし)

### 描画 (src/Renderer.cpp)
シャドウマップ → 床 → (色・深度のコピー) → 水の裏面深度 → 水面 (フレネル反射 + スクリーン空間屈折 +
厚みによる吸収 + ハイライト) → ガラス水槽 → 2×SSAA 縮小・トーンマップ・周辺減光。

## 解説書

`book/CG解説書.pdf` (約 110 ページ) に、SPH 法・マーチングキューブ法・水のレンダリングの理論と
本プログラムのコードを対応付けて解説した教科書があります。
`python book/build_book.py` で章 (`book/chapters/*.html`) からソースコードを自動抽出して HTML と PDF を再生成できます
(ヘッドレス Chrome を使用)。
