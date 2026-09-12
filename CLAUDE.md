# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## プロジェクト概要

動画「【SPH法】水しぶきシミュレーション」を再現する DirectX 11 / C++17 / HLSL 5.0 アプリ。
GPU WCSPH (コンピュートシェーダー) + GPU マーチングキューブ法で水を計算・描画する。
コメント・ドキュメントは日本語で書く (既存コードに合わせる)。

## ビルド・実行

```
build.bat                 # リリースビルド → build\SPHSplash.exe, build\mc_selftest.exe
build.bat debug           # デバッグビルド (/Od /Zi /MDd)
build\mc_selftest.exe     # 唯一の自動テスト: MC 三角形テーブルの自己検証 (終了コード 0 = PASS)
build\SPHSplash.exe                                   # リアルタイム表示
build\SPHSplash.exe --record capture --frames 102     # 24fps 相当で BMP 連番保存して終了 (動画は 102 フレーム)
build\SPHSplash.exe --spacing 0.010                   # 粒子間隔を細かく (粒子数 ∝ 1/spacing³)
build\SPHSplash.exe --resttest                        # 水塊を床に静置する安定性テストシーン
build\SPHSplash.exe --particles                       # 粒子を点で描くデバッグ表示 (P キーでも切替)
```

- `build.bat` は vcvars64.bat を VS 18 → 2022 の順に探す。**ASCII のみで書くこと** (cmd.exe は UTF-8 の日本語コメントを解釈できずパースエラーになる)。
- ソースは UTF-8 (BOM 無し) で `/utf-8` を付けてコンパイル。HLSL の日本語コメントは D3DCompileFromFile で問題なく通る。
- CMakeLists.txt は代替手段。cmake は PATH に無いので通常は build.bat を使う。
- シェーダーは実行時コンパイル。`<exe>/shaders/` → `<exe>/../shaders/` → `shaders/` の順に探すので、`build/` から実行しても `shaders/` の編集がそのまま反映される (再ビルド不要)。
- コンパイルエラーは stderr と MessageBox に出る。Bash から実行する場合 `2>&1 | tail` で拾える。
- 描画結果の確認は `--record` した BMP を Python (cv2) で PNG に変換して画像として読む。cv2 は anaconda の python に入っている。
- 本体は `/SUBSYSTEM:CONSOLE` なので printf の統計 (`[SPH] particles=...`, `[MC] grid=...`) がコンソールに出る。

## アーキテクチャ

### フレームの流れ (main.cpp)
1 描画フレーム = 動画の 1/24 秒 (`cfg::FRAME_DT`) の固定ステップ。実時間には依存しない。

```
StepFrame()  = Step() × SubstepsPerFrame()      // SPH
SortParticles()                                  // MC が近傍探索できるよう格子を最新化 (必須)
MarchingCubes::Build(sim)                        // 粒子 → スカラー場 → 三角形バッファ
Renderer::Render(sim, mc, time) → Present
```

サブステップ数は `dt = CFL·h/c` から自動決定 (`SPHSimulator::Initialize`)。`spacing` を変えると h, dt, 粒子質量, 格子寸法がすべて派生する。

### SPHSimulator (src/SPHSimulator.*, shaders/sph.hlsl)
1 サブステップ = 7 回の Dispatch: ClearCells → Count → Scan (1 グループの累積和) → Reorder → Density → Force → Integrate。
- 位置・速度は**ピンポンバッファ** (`m_pos[2]`, `m_vel[2]`, `m_cur`)。Reorder と Integrate で入れ替わる。`PositionSRV()` は常に「直近にソートされた順」を返す。
- CS_Force は XSPH を `u5` (gVelOut) に書くが、C++ 側はそこに専用の `m_xsph` を束縛している (速度バッファではない)。
- 各 Dispatch 後に `D3DContext::UnbindCompute()` を呼ぶ規約 (SRV/UAV 同時束縛の警告回避)。UAV は u0–u7 のみ使用 (FL 11.0 の上限)。
- 境界: 水槽壁は `tankHeight` 以下でのみ有効。壁近傍の密度欠損は解析的な「ゴースト密度」で補う。Monaghan 人工粘性 (`ARTIFICIAL_VISC_ALPHA`) が無いと衝突時に粒子が飛散するので外さないこと。
- `SimParams` (C++) と `cbuffer SimParams` (sph_common.hlsli) は**手動で同期**。メンバを足すときは両方 + 16 byte パッキングを確認。`MCParams`/`FrameParams` も同様。

### MarchingCubes (src/MarchingCubes.*, src/MCTables.*, shaders/mc.hlsl)
- 三角形テーブルは Bourke の表をコピーせず `mc::GenerateTriangleTable()` で**起動時に生成**する (面ごとに交差エッジを辿って閉ループ化、曖昧面は「内側頂点を分離」で統一、扇状分割)。起動時に `mc::SelfTest()` (球で閉多様体・外向きを検証) を通し、失敗したら例外。
- 頂点/エッジ番号の規約 (頂点 v = (v&1, v>>1&1, v>>2&1)、エッジ 0–3 x / 4–7 y / 8–11 z) は MCTables.h と mc.hlsl の `EDGE_VERTS` で一致させる必要がある。
- 出力は `AppendStructuredBuffer<MCTriangle>` (72 byte) → `CopyStructureCount` → `CS_MakeArgs` → `DrawInstancedIndirect`。CPU 往復なし。`LastTriangleCount()` だけは統計用に GPU 同期する。
- スカラー場 = Σ(1 − r²/R²)³、R = h。MC 格子は SPH 格子より細かい (`MC_CELL_PER_SPACING = 0.75`) が、近傍探索には SPH の `cellStart` を再利用する (cbuffer `b0` に `SimParams` を束縛)。
- 頂点順は外向き = D3D 既定の CW 表面。Renderer は CullBack で描く。

### Renderer (src/Renderer.*, shaders/water.hlsl, scene.hlsl, post.hlsl)
パス順: シャドウマップ → 床 (HDR RT) → 色・深度を CopyResource → 水の**裏面深度** (CullFront) → 水面 → ガラス (乗算済みアルファ) → ポスト (2×SSAA 縮小 + トーンマップ + 周辺減光)。
- 水は不透明として描き、屈折は直前にコピーしたシーン色をスクリーン空間でずらして参照。厚み = `min(背景深度, 裏面深度) − 表面深度` で吸収を掛ける。
- 行列は転置して cbuffer に入れ、HLSL は `mul(vec, M)`。
- 水槽の衝突壁 (`TANK_HEIGHT` = 1.6) と描画するガラス (`GLASS_VISIBLE_HEIGHT` = 0.3) は高さが違う。動画では壁が見えない透明な衝突箱のため。

### スケール (SimConfig.h)
動画の水槽は幅 ≒ 2 m と推定されるが、幾何は 1 m 角で作り `GRAVITY = 9.81 / SCENE_SCALE` (フルード相似) で同じ時間経過にしている。シーンを実寸にしたくなったら粒子数が 8 倍になることに注意。
見た目・物理の調整はほぼ全て `src/SimConfig.h` の定数で行う。水の色は `shaders/water.hlsl` の `absorb`。

## ドキュメント
- `README.md`: 使い方と手法の概要
- `docs/API仕様書.md` (+ `.html`, `.pdf`): 公開 API、HLSL のスロット束縛表、フロー図。API を変えたら更新する。PDF は `docs/API仕様書.html` を headless Chrome で `--print-to-pdf` して生成している。
