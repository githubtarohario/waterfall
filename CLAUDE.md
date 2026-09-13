# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## プロジェクト概要

動画「【SPH法】水しぶきシミュレーション」を再現する DirectX 11 / C++17 / HLSL 5.0 アプリ。
GPU 上の WCSPH (弱圧縮性 SPH) で粒子を計算し、GPU マーチングキューブで水面メッシュを作って描画する。
すべてのコメント・ドキュメントは日本語で書く (既存コードの慣習)。

詳細な API とフロー図は `docs/API仕様書.md` にある。手法の背景は `README.md`。

## ビルド・実行・テスト

```
build.bat                 # リリースビルド → build\SPHSplash.exe, build\mc_selftest.exe
build.bat debug           # デバッグビルド (/Od /Zi /MDd)
build\mc_selftest.exe     # 唯一の単体テスト: MC 三角形テーブル生成の自己検証 (終了コード 0 = 合格)
build\SPHSplash.exe       # リアルタイム表示 (Space 停止, R リセット, P 粒子表示, W 水面, G ガラス, Esc)
build\SPHSplash.exe --record capture --frames 102   # 動画と同じ 24fps 相当で 4.25 秒分を BMP 連番保存して終了
build\SPHSplash.exe --spacing 0.010                 # 粒子間隔を細かく (≒15万粒子, 既定 0.014 で ≒5万)
build\SPHSplash.exe --resttest                      # 水塊を床に静置する安定性テストシーン
build\SPHSplash.exe --particles --nowater           # 粒子だけを表示 (メッシュ無し)
python book/build_book.py                          # 解説書 book/CG解説書.{html,pdf} を再生成 (章は book/chapters/, コードは実物から自動抽出)
```

- `build.bat` は Visual Studio 18 / 2022 の `vcvars64.bat` を探す。**バッチファイルは ASCII のみ** (cmd.exe が UTF-8 の日本語を解釈できず構文エラーになる)。C++ ソースは UTF-8 で `/utf-8` 付きコンパイル。
- CMake 派: `cmake -S . -B build_cmake -G "Visual Studio 17 2022" -A x64 && cmake --build build_cmake --config Release`
- シェーダーは実行時に `D3DCompileFromFile` でコンパイルされる。`.hlsl` の変更に再ビルドは不要 (exe の隣 → `..\shaders\` → カレントの順に探索)。コンパイルエラーは stderr と MessageBox に出て終了コード 1。
- 変更の視覚確認は `--record --frames N` で BMP を出し、Python (cv2) で PNG に変換して見るのが実績のある手順。`build/` と `capture*/` は git 管理外。

## アーキテクチャ

1 描画フレーム = 動画の 1/24 秒 (`cfg::FRAME_DT`) の固定ステップ。実時間には依存しない。
`main.cpp` のループ: `sim.StepFrame()` → `sim.SortParticles()` → `mc.Build(sim)` → `renderer.Render(...)` → Present。

### 設定は SimConfig.h に集約
シーン配置 (`WATER_BLOCKS`)、水槽寸法、SPH 定数、MC 設定、カメラ・ライトはすべて `src/SimConfig.h` の `cfg::` 定数。
調整はまずここを変える。粒子間隔 `spacing` から h・dt・サブステップ数・格子・質量が実行時に導出される
(`SPHSimulator::Initialize`)。**重力は `9.81 / SCENE_SCALE`**: 動画の水槽 (推定 2 m) を 1 m でモデル化し、
フルード相似で時間経過を合わせている。粒子数を変えずに「速さ」を調整したいときはここ。

### C++ と HLSL の構造体は手動で一致させる
- `SimParams` (SPHSimulator.h) ⇔ `cbuffer SimParams` (sph_common.hlsli, `b0`)
- `MCParams` (MarchingCubes.h) ⇔ `cbuffer MCParams` (mc.hlsl, `b1`)
- `FrameParams` (Renderer.h) ⇔ `cbuffer FrameParams` (render_common.hlsli, `b0`)
- `MCTriangle {MCVertex v0,v1,v2}` (mc.hlsl の Append 出力, 72 byte) ⇔ water.hlsl の入力
メンバを追加するときは両側を同じ順序・同じ 16 byte パッキングで更新する。行列は転置して渡し HLSL 側は `mul(vec, M)`。

### SPH (SPHSimulator + sph.hlsl)
1 サブステップ = 7 Dispatch: ClearCells → Count (atomic) → Scan (1024 スレッド × 1 グループの逐次チャンク) → Reorder → Density → Force → Integrate。
位置・速度は **ピンポンバッファ** (`m_pos[2]`, `m_vel[2]`, `m_cur`)。Reorder と Integrate で入れ替わる。
`CS_Force` は `u5` (シェーダー名 `gVelOut`) に XSPH 補正を書く — C++ 側で `m_xsph` を束縛しているため速度バッファではない。
各 Dispatch 後に `UnbindCompute()` で SRV/UAV を全解除する規約 (同一リソースの SRV/UAV 同時束縛を避ける)。UAV は `u0–u7` に収める (FL 11.0)。

物理の安定化: Tait 圧力は負圧を 0 にクランプ、壁近傍は解析的なゴースト密度で補う、Monaghan 人工粘性 α (`ARTIFICIAL_VISC_ALPHA`) が衝撃時の粒子飛散を抑える (これが無いと着水時に爆発する)、速度は `0.5 h/dt` で上限。
水槽の壁は衝突用に `TANK_HEIGHT` (1.6 m, 領域上端) まであるが、描画は `GLASS_VISIBLE_HEIGHT` (0.3 m) まで。動画では壁がほぼ見えないため。

### マーチングキューブ (MarchingCubes + MCTables + mc.hlsl)
三角形テーブルは Bourke 表を埋め込まず **起動時に `mc::GenerateTriangleTable()` で生成** し `mc::SelfTest()` (球で閉多様体・外向きを検証) に失敗すると例外。
頂点番号は `v = (x, y, z) = (bit0, bit1, bit2)`、エッジ 0–3 = x, 4–7 = y, 8–11 = z 方向。**C++ の `EDGE_VERTS` と mc.hlsl の `EDGE_VERTS` は同一でなければならない。**
`Build()` は SPH の格子 (`PositionSRV`, `CellStartSRV`) を近傍探索に再利用するので、**呼び出し前に `SortParticles()` が必須** (Integrate 後は位置と格子がずれている)。
出力は AppendStructuredBuffer → `CopyStructureCount` → `CS_MakeArgs` → `DrawInstancedIndirect`。CPU 読み戻しは統計表示 (`LastTriangleCount`) のみ。
法線は −∇F、三角形の巻き順は外向き (左手系 + `FrontCounterClockwise=FALSE` で表になる)。

### 描画 (Renderer + water/scene/post.hlsl)
シャドウマップ → 床 (HDR RT) → 色・深度コピー → 水の裏面深度 (CullFront) → 水面 → ガラス (乗算済みアルファ) → ポスト (2×SSAA 縮小・トーンマップ・周辺減光)。
水の厚み = `min(背景深度, 裏面深度) − 表面深度` で吸収を計算。水の見た目 (吸収係数・ハイライト) は `water.hlsl`、環境光・ソフトボックスの強さは `render_common.hlsli` の `EnvColor`、床の明るさは `scene.hlsl`。
SSAA のため内部解像度はウィンドウの 2 倍; スクリーン空間の計算は `screenSize` (内部解像度) を使う。

## 関連

- リポジトリ: https://github.com/githubtarohario/waterfall (公開)
- 参考動画: `C:\Users\PCUSER\Downloads\【SPH法】水しぶきシミュレーション _ Water Splash Simulation.mp4` (640×360, 24fps, 102 フレーム)。忠実度の確認はこの動画のフレームと `--record` 出力を並べて比較する。
