// =====================================================================
//  sph_common.hlsli
//  SPH 計算シェーダー共通定義 (定数バッファ / 格子 / カーネル関数)
//  C++ 側の SimParams 構造体 (SPHSimulator.h) と完全に一致させること。
// =====================================================================
#ifndef SPH_COMMON_HLSLI
#define SPH_COMMON_HLSLI

cbuffer SimParams : register(b0)
{
    float3 domainMin;   float cellSize;      // 領域最小 / 格子セル幅 (= h)
    float3 domainMax;   float h;             // 領域最大 / 平滑化長
    uint3  gridDim;     uint  numCells;      // 格子の分割数 / 総セル数
    float  h2;          float mass;          // h^2 / 粒子質量
    float  restDensity; float taitB;         // 基準密度 / Tait 方程式の剛性 B
    float  viscosity;   float xsphEps;       // 粘性係数 μ / XSPH 係数
    float  dt;          float gravity;       // サブステップ時間 / 重力加速度
    float  poly6;       float spikyGrad;     // カーネル正規化定数
    float  viscLap;     float tankInner;     // 粘性ラプラシアン定数 / 水槽内側半幅
    float  tankOuter;   float tankHeight;    // 水槽外側半幅 / 壁の高さ
    float  tankFloorY;  float floorY;        // 水槽内の床 / スタジオ床
    float  restitution; float friction;      // 壁の反発係数 / 接線減衰
    float  maxSpeed;    float boundaryDensityScale; // 速度上限 / 境界密度補正
    uint   numParticles; float artificialAlpha;   // 粒子数 / Monaghan 人工粘性 α
    float  cohesion;     float soundSpeed;         // 凝集力 κ / 数値音速 c
};

// ---------------------------------------------------------------------
// 一様格子
// ---------------------------------------------------------------------
// 位置 → 格子座標 (領域外は端のセルへクランプ)
int3 CellCoord(float3 p)
{
    int3 c = (int3)floor((p - domainMin) / cellSize);
    return clamp(c, int3(0, 0, 0), int3(gridDim) - 1);
}

// 格子座標 → 1 次元セル番号
uint CellId(int3 c)
{
    return (uint)c.x + (uint)c.y * gridDim.x + (uint)c.z * gridDim.x * gridDim.y;
}

// 格子座標が有効範囲内か
bool CellValid(int3 c)
{
    return all(c >= 0) && all(c < int3(gridDim));
}

// ---------------------------------------------------------------------
// カーネル関数 (Müller et al. 2003)
//   密度   : poly6   W(r) = 315/(64π h^9) (h^2 - r^2)^3
//   圧力   : spiky   ∇W(r) = -45/(π h^6) (h - r)^2 r̂
//   粘性   : viscosity  ∇²W(r) = 45/(π h^6) (h - r)
// ---------------------------------------------------------------------
float Poly6(float r2)
{
    float t = h2 - r2;
    return poly6 * t * t * t;
}

// ---------------------------------------------------------------------
// 境界 (壁) からの距離。水槽の内側・外側で参照する壁が変わる。
// ---------------------------------------------------------------------
bool InsideTankFootprint(float3 p)
{
    return abs(p.x) < tankInner && abs(p.z) < tankInner;
}

#endif // SPH_COMMON_HLSLI
