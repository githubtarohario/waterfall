#pragma once
// =====================================================================
//  SimConfig.h
//  シミュレーション / シーン / 描画に関する調整パラメータをここに集約する。
//  動画「【SPH法】水しぶきシミュレーション」の構図を再現するための値。
//
//  座標系: 右手系ではなく DirectX 標準の左手系。Y が上。
//          水槽の中心が原点、水槽の内側の床が y = 0。単位は [m]。
// =====================================================================
#include <cstdint>

namespace cfg
{
    // ----------------------------------------------------------------
    // 表示・時間
    // ----------------------------------------------------------------
    constexpr int   WINDOW_W  = 1280;             // ウィンドウ幅 (動画と同じ 16:9)
    constexpr int   WINDOW_H  = 720;              // ウィンドウ高さ
    constexpr int   SSAA      = 2;                // スーパーサンプリング倍率 (2 = 縦横2倍で描画して縮小)
    constexpr float VIDEO_FPS = 24.0f;            // 動画と同じ 24fps
    constexpr float FRAME_DT  = 1.0f / VIDEO_FPS; // 1 描画フレームで進める物理時間 [s]
    constexpr float TOTAL_TIME = 4.25f;           // 動画の長さ [s] (102 フレーム)

    // ----------------------------------------------------------------
    // SPH (Weakly Compressible SPH, Tait 状態方程式)
    // ----------------------------------------------------------------
    constexpr float DEFAULT_SPACING = 0.014f;     // 初期配置の粒子間隔 d [m]  (--spacing で変更可)
    constexpr float H_PER_SPACING   = 2.0f;       // 平滑化長 h = d * 2 (近傍 ≒ 33 個)
    constexpr float REST_DENSITY    = 1000.0f;    // 基準密度 ρ0 [kg/m^3]
    constexpr float SOUND_SPEED     = 12.0f;      // 数値音速 c [m/s] (最大流速の ~4 倍。密度変動 ≒ 数%)
    constexpr float TAIT_GAMMA      = 7.0f;       // Tait 方程式の指数 γ
    constexpr float VISCOSITY_MU    = 0.5f;       // 物理粘性 μ [Pa·s] (Müller のラプラシアン型。減衰率 ≒ 22μ /s)
    constexpr float ARTIFICIAL_VISC_ALPHA = 0.08f;// Monaghan 人工粘性 α (接近する粒子対の衝撃を減衰し飛散を抑える)
    constexpr float COHESION_KAPPA  = 0.0f;       // 凝集力 κ (Becker & Teschner)。0 で無効
    constexpr float XSPH_EPS        = 0.15f;      // XSPH 速度平滑化係数
    // 動画の水槽は落下速度から推定すると幅 ≒ 2m。粒子数を抑えるため幾何は 1m 角で作り、
    // フルード相似 (t ∝ sqrt(L/g)) により重力を 1/SCENE_SCALE にして同じ時間経過を再現する。
    constexpr float SCENE_SCALE     = 2.0f;       // 実寸 / モデル寸法
    constexpr float GRAVITY         = 9.81f / SCENE_SCALE; // 相似則で縮小した重力加速度 [m/s^2]
    constexpr float CFL             = 0.4f;       // クーラン数: dt = CFL * h / c
    constexpr float WALL_RESTITUTION = 0.05f;     // 壁衝突の反発係数
    constexpr float WALL_FRICTION   = 0.999f;     // 壁接触中の接線速度減衰 (サブステップ毎。ガラスは滑りやすい)
    constexpr float BOUNDARY_DENSITY_SCALE = 1.0f;// 壁近傍で欠損する近傍分の密度補正の強さ
    constexpr float MAX_SPEED_PER_H = 0.5f;       // 1 サブステップで h の 50% 以上動かないよう速度制限

    // ----------------------------------------------------------------
    // 水槽 (ガラス製・上面開放)。動画では壁がほとんど見えないため、
    // 描画はごく薄いフレネル反射のみ (G キーで表示切替)。
    // ----------------------------------------------------------------
    constexpr float TANK_INNER_HALF     = 0.50f;  // 内側の半幅 (内寸 1.0m 角)
    constexpr float TANK_WALL_THICKNESS = 0.02f;  // ガラス壁の厚さ
    constexpr float TANK_OUTER_HALF     = TANK_INNER_HALF + TANK_WALL_THICKNESS;
    constexpr float TANK_HEIGHT         = 1.60f;  // 衝突用の壁の高さ (動画では見えない透明な衝突箱。水は外へ出ない)
    constexpr float GLASS_VISIBLE_HEIGHT = 0.30f; // 描画するガラス壁の高さ (動画で僅かに見える縁の部分だけ)
    constexpr float TANK_FLOOR_Y        = 0.0f;   // 水槽内側の床の高さ
    constexpr float FLOOR_Y             = -TANK_WALL_THICKNESS; // スタジオ床 (ガラス底板の下面)

    // ----------------------------------------------------------------
    // シミュレーション領域 (粒子はこの AABB の外へ出ない)
    // ----------------------------------------------------------------
    constexpr float DOMAIN_MIN[3] = { -0.75f, FLOOR_Y, -0.75f };
    constexpr float DOMAIN_MAX[3] = {  0.75f, 1.60f,   0.75f };

    // ----------------------------------------------------------------
    // 初期の水塊
    //   現在は水槽中央の上空に 1 つだけ配置 (約 0.5 秒後に着地)。
    //   動画と同じ 2 つ構成に戻すには、下のコメントアウトを有効にする:
    //     A: 手前左の薄くて背の高いブロック (すぐ着水する)
    //     B: 奥右の大きなブロック (少し遅れて A の水面へ落ちる)
    // ----------------------------------------------------------------
    struct WaterBlock { float mn[3]; float mx[3]; };
    constexpr WaterBlock WATER_BLOCKS[] = {
        { { -0.30f, 0.60f, -0.30f }, { 0.30f, 1.10f, 0.30f } },  // 中央の直方体 1 つ (0.6 x 0.5 x 0.6)
        // { { -0.30f, 0.22f,  0.10f }, { 0.20f, 0.82f, 0.35f } }, // A (x に長く z に薄い。約 0.3 秒後に着地)
        // { { -0.15f, 0.95f, -0.42f }, { 0.40f, 1.40f, -0.10f } }, // B (やや大きい箱。約 0.55 秒後に着水)
    };
    constexpr int NUM_WATER_BLOCKS = sizeof(WATER_BLOCKS) / sizeof(WATER_BLOCKS[0]);

    // ----------------------------------------------------------------
    // マーチングキューブ法
    // ----------------------------------------------------------------
    constexpr float MC_CELL_PER_SPACING = 0.75f;  // MC 格子の間隔 = d * 0.75
    constexpr float MC_FIELD_RADIUS_PER_SPACING = 2.0f; // スカラー場カーネル半径 R = d * 2 (= h)
    constexpr float MC_ISO_LEVEL  = 0.5f;         // 等値面レベル (単独粒子の中心値が 1.0、平滑化 2 回後 ≒0.55)
    constexpr int   MC_SMOOTH_PASSES = 2;         // スカラー場の 7 点平滑化の回数 (粒子由来の凹凸を抑える)
    constexpr uint32_t MC_MAX_TRIANGLES = 1u << 20; // 三角形バッファ容量 (1M 個)

    // ----------------------------------------------------------------
    // カメラ・ライト (動画の構図に合わせた値)
    // ----------------------------------------------------------------
    constexpr float CAM_TARGET[3]  = { -0.25f, 0.54f, 0.07f };  // 注視点 (水槽中心が画面の (47%, 68%) に来る位置)
    constexpr float CAM_YAW_DEG    = -45.0f;      // 水槽の角が手前に来るコーナー視点 (カメラは -x,+z 側)
    constexpr float CAM_PITCH_DEG  = 30.0f;       // 見下ろし角
    constexpr float CAM_DISTANCE   = 3.2f;        // 注視点からの距離
    constexpr float CAM_FOV_DEG    = 40.0f;       // 垂直画角
    constexpr float LIGHT_DIR[3]   = { 0.15f, 1.0f, -0.9f }; // 光源へ向かうベクトル (影は手前左へ落ちる)
}
