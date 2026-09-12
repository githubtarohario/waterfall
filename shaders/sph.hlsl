// =====================================================================
//  sph.hlsl
//  WCSPH (弱圧縮性 SPH) の 1 サブステップを構成する計算シェーダー群。
//
//  1 サブステップの流れ (C++ 側 SPHSimulator::Step が順に Dispatch する):
//    CS_ClearCells  : 格子セルのカウンタを 0 にする
//    CS_Count       : 各粒子の所属セルを求め、セル内粒子数をカウント
//    CS_Scan        : セルカウントの排他的累積和 → 各セルの先頭インデックス
//    CS_Reorder     : 粒子をセル順に並べ替え (メモリの局所性向上)
//    CS_Density     : 近傍探索で密度を計算し Tait 方程式で圧力を求める
//    CS_Force       : 圧力項・粘性項・重力の加速度と XSPH 補正速度を計算
//    CS_Integrate   : 時間積分と壁・床・水槽との衝突処理
// =====================================================================
#include "sph_common.hlsli"

#define THREADS 256

// ---- 入力 (SRV) ----
StructuredBuffer<float4> gPosIn      : register(t0);  // 位置 (xyz)
StructuredBuffer<float4> gVelIn      : register(t1);  // 速度 (xyz)
StructuredBuffer<uint>   gCellStart  : register(t2);  // セルの先頭インデックス (numCells+1 個)
StructuredBuffer<float2> gDensPres   : register(t3);  // x = 密度, y = 圧力
StructuredBuffer<float4> gAccel      : register(t4);  // 加速度
StructuredBuffer<float4> gXsph       : register(t5);  // XSPH 速度補正
StructuredBuffer<uint>   gCellIndex  : register(t6);  // 粒子の所属セル番号
StructuredBuffer<uint>   gCellOffset : register(t7);  // セル内でのオフセット

// ---- 出力 (UAV) ----
RWStructuredBuffer<uint>   gCellCount   : register(u0);
RWStructuredBuffer<uint>   gCellStartRW : register(u1);
RWStructuredBuffer<uint>   gCellIndexRW : register(u2);
RWStructuredBuffer<uint>   gCellOffsetRW: register(u3);
RWStructuredBuffer<float4> gPosOut      : register(u4);
RWStructuredBuffer<float4> gVelOut      : register(u5);
RWStructuredBuffer<float2> gDensPresRW  : register(u6);
RWStructuredBuffer<float4> gAccelRW     : register(u7);

// ---------------------------------------------------------------------
// 1. セルカウンタのクリア
// ---------------------------------------------------------------------
[numthreads(THREADS, 1, 1)]
void CS_ClearCells(uint3 id : SV_DispatchThreadID)
{
    if (id.x < numCells) gCellCount[id.x] = 0;
}

// ---------------------------------------------------------------------
// 2. 粒子の所属セルを求めてカウント (アトミック加算で順序番号を確保)
// ---------------------------------------------------------------------
[numthreads(THREADS, 1, 1)]
void CS_Count(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= numParticles) return;
    uint cell = CellId(CellCoord(gPosIn[i].xyz));
    uint offset;
    InterlockedAdd(gCellCount[cell], 1, offset);
    gCellIndexRW[i]  = cell;
    gCellOffsetRW[i] = offset;
}

// ---------------------------------------------------------------------
// 3. 排他的累積和 (単一グループで 1024 要素ずつ順に処理)
//    セル数は数万程度なので単一グループでも十分高速。
// ---------------------------------------------------------------------
#define SCAN_THREADS 1024
groupshared uint sScan[SCAN_THREADS];
groupshared uint sCarry;

[numthreads(SCAN_THREADS, 1, 1)]
void CS_Scan(uint tid : SV_GroupThreadID)
{
    if (tid == 0) sCarry = 0;
    GroupMemoryBarrierWithGroupSync();

    for (uint base = 0; base < numCells; base += SCAN_THREADS)
    {
        uint i = base + tid;
        uint v = (i < numCells) ? gCellCount[i] : 0;
        sScan[tid] = v;
        GroupMemoryBarrierWithGroupSync();

        // Hillis-Steele 包括的スキャン
        [unroll]
        for (uint offset = 1; offset < SCAN_THREADS; offset <<= 1)
        {
            uint t = (tid >= offset) ? sScan[tid - offset] : 0;
            GroupMemoryBarrierWithGroupSync();
            sScan[tid] += t;
            GroupMemoryBarrierWithGroupSync();
        }

        if (i < numCells) gCellStartRW[i] = sCarry + sScan[tid] - v;   // 排他的にするため自身を引く
        GroupMemoryBarrierWithGroupSync();
        if (tid == SCAN_THREADS - 1) sCarry += sScan[tid];              // このチャンクの合計を持ち越す
        GroupMemoryBarrierWithGroupSync();
    }
    if (tid == 0) gCellStartRW[numCells] = sCarry;   // 末尾 = 総粒子数
}

// ---------------------------------------------------------------------
// 4. セル順への並べ替え
// ---------------------------------------------------------------------
[numthreads(THREADS, 1, 1)]
void CS_Reorder(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= numParticles) return;
    uint dst = gCellStart[gCellIndex[i]] + gCellOffset[i];
    gPosOut[dst] = gPosIn[i];
    gVelOut[dst] = gVelIn[i];
}

// ---------------------------------------------------------------------
// 壁近傍で失われる近傍粒子分の密度を補う (ゴースト密度)
//   平面境界からの距離 d < h のとき、欠損分 ≒ ρ0 * 0.5 * (1 - d/h)^2
// ---------------------------------------------------------------------
float WallDensity(float dist)
{
    float t = saturate(1.0 - dist / h);
    return restDensity * 0.5 * t * t;
}

float BoundaryDensity(float3 p)
{
    float rho = 0.0;
    bool inside = InsideTankFootprint(p);
    // 床
    rho += WallDensity(p.y - (inside ? tankFloorY : floorY));
    // 水槽の壁 (壁の高さより低い場合)
    if (p.y < tankHeight)
    {
        if (inside)
        {
            rho += WallDensity(tankInner - abs(p.x));
            rho += WallDensity(tankInner - abs(p.z));
        }
        else
        {
            // 外側: 外壁面までの距離 (壁の帯の外側にいる軸のみ)
            if (abs(p.x) >= tankOuter && abs(p.z) < tankOuter) rho += WallDensity(abs(p.x) - tankOuter);
            if (abs(p.z) >= tankOuter && abs(p.x) < tankOuter) rho += WallDensity(abs(p.z) - tankOuter);
        }
    }
    return rho * boundaryDensityScale;
}

// ---------------------------------------------------------------------
// 5. 密度と圧力
// ---------------------------------------------------------------------
[numthreads(THREADS, 1, 1)]
void CS_Density(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= numParticles) return;

    float3 pi = gPosIn[i].xyz;
    int3   c  = CellCoord(pi);
    float  sum = 0.0;

    // 27 近傍セルを走査
    for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
    {
        int3 nc = c + int3(dx, dy, dz);
        if (!CellValid(nc)) continue;
        uint cell = CellId(nc);
        uint s = gCellStart[cell], e = gCellStart[cell + 1];
        for (uint j = s; j < e; ++j)
        {
            float3 r  = pi - gPosIn[j].xyz;
            float  r2 = dot(r, r);
            if (r2 < h2)
            {
                float t = h2 - r2;
                sum += t * t * t;     // poly6 (自分自身の寄与も含む)
            }
        }
    }

    float rho = mass * poly6 * sum + BoundaryDensity(pi);
    rho = max(rho, restDensity * 0.1);

    // Tait 方程式: p = B ((ρ/ρ0)^7 - 1)。負圧は 0 にクランプ (クラスタリング防止)
    float ratio = rho / restDensity;
    float r2_ = ratio * ratio;
    float r7  = r2_ * r2_ * r2_ * ratio;
    float p   = max(0.0, taitB * (r7 - 1.0));

    gDensPresRW[i] = float2(rho, p);
}

// ---------------------------------------------------------------------
// 6. 力の計算 (加速度) と XSPH 補正
//    出力: gAccelRW (u7) = 加速度, gVelOut (u5) = XSPH 補正速度
//    ※ u5 には C++ 側で専用の XSPH バッファを束縛する (速度バッファではない)
// ---------------------------------------------------------------------
[numthreads(THREADS, 1, 1)]
void CS_Force(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= numParticles) return;

    float3 pi   = gPosIn[i].xyz;
    float3 vi   = gVelIn[i].xyz;
    float2 dpi  = gDensPres[i];
    float  rhoi = dpi.x, presi = dpi.y;
    float  pTermI = presi / (rhoi * rhoi);
    int3   c    = CellCoord(pi);

    float3 aPress = 0.0;   // 圧力項
    float3 aVisc  = 0.0;   // 粘性項 (ラプラシアン型)
    float3 aArt   = 0.0;   // Monaghan 人工粘性
    float3 aCoh   = 0.0;   // 凝集力
    float3 xsph   = 0.0;   // XSPH

    for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
    {
        int3 nc = c + int3(dx, dy, dz);
        if (!CellValid(nc)) continue;
        uint cell = CellId(nc);
        uint s = gCellStart[cell], e = gCellStart[cell + 1];
        for (uint j = s; j < e; ++j)
        {
            if (j == i) continue;
            float3 r  = pi - gPosIn[j].xyz;
            float  r2 = dot(r, r);
            if (r2 >= h2 || r2 < 1e-12) continue;

            float  rl   = sqrt(r2);
            float2 dpj  = gDensPres[j];
            float  rhoj = dpj.x;
            float3 vj   = gVelIn[j].xyz;
            float  t    = h - rl;

            // 圧力 (Monaghan の対称形): a = -Σ m (pi/ρi² + pj/ρj²) ∇W_spiky
            float3 gradW = spikyGrad * t * t * (r / rl);      // spikyGrad は負の定数
            aPress -= mass * (pTermI + dpj.y / (rhoj * rhoj)) * gradW;

            // 粘性: f = μ Σ m (vj - vi)/ρj ∇²W_visc
            aVisc += (vj - vi) * (mass / rhoj) * (viscLap * t);

            // Monaghan 人工粘性: 接近中 (v·r < 0) の粒子対にだけ働く衝撃減衰
            //   Π = -α c h (v·r) / (|r|² + 0.01h²) / ρ̄,  a -= Σ m Π ∇W
            float vr = dot(vi - vj, r);
            if (vr < 0.0)
            {
                float piij = -artificialAlpha * soundSpeed * h * vr / (r2 + 0.01 * h2) / (0.5 * (rhoi + rhoj));
                aArt -= mass * piij * gradW;
            }

            // 凝集力 (Becker & Teschner): a = -κ Σ (xi - xj) W_poly6
            float w = h2 - r2;
            aCoh -= r * (poly6 * w * w * w);

            // XSPH: Σ (m/ρj)(vj - vi) W_poly6
            xsph += (vj - vi) * (mass / rhoj) * (poly6 * w * w * w);
        }
    }

    float3 accel = aPress + viscosity * aVisc / rhoi + aArt + cohesion * aCoh + float3(0.0, -gravity, 0.0);
    gAccelRW[i] = float4(accel, 0.0);
    gVelOut[i]  = float4(xsph, 0.0);
}

// ---------------------------------------------------------------------
// 7. 時間積分 (シンプレクティック・オイラー) と境界処理
//    gPosOut/gVelOut は入力とは別のバッファ (ピンポン)。C++ 側で入れ替える。
// ---------------------------------------------------------------------
[numthreads(THREADS, 1, 1)]
void CS_Integrate(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= numParticles) return;

    float3 pOld = gPosIn[i].xyz;
    float3 v    = gVelIn[i].xyz + gAccel[i].xyz * dt;

    // 速度上限 (爆発防止)
    float speed = length(v);
    if (speed > maxSpeed) v *= maxSpeed / speed;

    // XSPH 補正は移流にのみ使う
    float3 vAdv = v + xsphEps * gXsph[i].xyz;
    float3 p    = pOld + vAdv * dt;

    const float eps = 1e-4;
    bool wasInside = InsideTankFootprint(pOld);

    // --- 水槽の壁 (壁の高さより低い位置でのみ有効) ---
    if (p.y < tankHeight)
    {
        if (wasInside)
        {
            // 内側の粒子は内壁を越えない
            float lim = tankInner - eps;
            if (abs(p.x) > lim) { v.x = -v.x * restitution; v.yz *= friction; p.x = sign(p.x) * lim; }
            if (abs(p.z) > lim) { v.z = -v.z * restitution; v.xy *= friction; p.z = sign(p.z) * lim; }
        }
        else
        {
            // 外側の粒子はガラス壁の帯 (inner..outer) に侵入しない
            if (abs(p.x) < tankOuter && abs(p.z) < tankOuter)
            {
                float penX = tankOuter - abs(p.x);
                float penZ = tankOuter - abs(p.z);
                if (penX < penZ) { p.x = sign(p.x) * (tankOuter + eps); v.x = -v.x * restitution; }
                else             { p.z = sign(p.z) * (tankOuter + eps); v.z = -v.z * restitution; }
            }
        }
    }

    // --- 床 (水槽内はガラス底板の上、外はスタジオ床) ---
    float floorLevel = InsideTankFootprint(p) ? tankFloorY : floorY;
    if (p.y < floorLevel + eps)
    {
        p.y = floorLevel + eps;
        if (v.y < 0.0) v.y = -v.y * restitution;
        v.xz *= friction;
    }

    // --- シミュレーション領域の外壁 ---
    float3 lo = domainMin + eps, hi = domainMax - eps;
    if (p.x < lo.x) { p.x = lo.x; v.x = abs(v.x) * restitution; }
    if (p.x > hi.x) { p.x = hi.x; v.x = -abs(v.x) * restitution; }
    if (p.z < lo.z) { p.z = lo.z; v.z = abs(v.z) * restitution; }
    if (p.z > hi.z) { p.z = hi.z; v.z = -abs(v.z) * restitution; }
    if (p.y > hi.y) { p.y = hi.y; v.y = -abs(v.y) * restitution; }

    gPosOut[i] = float4(p, 0.0);
    gVelOut[i] = float4(v, 0.0);
}
