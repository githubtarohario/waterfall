// =====================================================================
//  mc.hlsl
//  粒子 → スカラー場 → マーチングキューブ法による等値面メッシュ生成。
//
//    CS_Field         : 各格子点でスカラー場 F(x) = Σ_j (1 - |x-x_j|²/R²)³ を計算
//    CS_MarchingCubes : 各セルで等値面を三角形化し AppendStructuredBuffer へ追加
//    CS_MakeArgs      : 三角形数 → DrawInstancedIndirect 用の引数バッファ
//
//  格子の頂点番号・エッジ番号は C++ 側 MCTables.h と同じ規約:
//    頂点 v の座標 = (v&1, (v>>1)&1, (v>>2)&1)
//    エッジ 0-3 = x 方向, 4-7 = y 方向, 8-11 = z 方向
// =====================================================================
#include "sph_common.hlsli"   // SPH の格子 (SimParams, b0) を近傍探索に再利用

cbuffer MCParams : register(b1)
{
    float3 mcOrigin;  float mcCell;         // 格子原点 / 格子間隔
    uint3  mcDim;     uint  mcNumVerts;     // 格子点数 (各軸) / 総格子点数
    float  isoLevel;  float fieldRadius;    // 等値面レベル / カーネル半径 R
    float  fieldInvR2; uint mcTableStride;  // 1/R² / 三角形テーブルのストライド
    uint   mcMaxTriangles; float3 mcPad;    // 三角形バッファ容量
};

StructuredBuffer<float4> gPos       : register(t0);   // 粒子位置 (セル順に整列済み)
StructuredBuffer<uint>   gCellStart : register(t2);   // セル先頭インデックス
StructuredBuffer<float>  gField     : register(t3);   // スカラー場
StructuredBuffer<int>    gTriTable  : register(t4);   // 三角形テーブル (256 * stride)

RWStructuredBuffer<float> gFieldRW  : register(u0);

// 出力頂点: 位置と法線
struct MCVertex   { float3 p; float3 n; };
struct MCTriangle { MCVertex v0, v1, v2; };
AppendStructuredBuffer<MCTriangle> gTriangles : register(u1);

RWByteAddressBuffer gTriCount : register(u2);   // CopyStructureCount の書き込み先
RWByteAddressBuffer gDrawArgs : register(u3);   // {VertexCountPerInstance, InstanceCount, StartVertex, StartInstance}

// 1 次元インデックス → 格子座標
uint3 VertexCoord(uint idx)
{
    uint x = idx % mcDim.x;
    uint y = (idx / mcDim.x) % mcDim.y;
    uint z = idx / (mcDim.x * mcDim.y);
    return uint3(x, y, z);
}

uint VertexIndex(uint3 c)
{
    return c.x + c.y * mcDim.x + c.z * mcDim.x * mcDim.y;
}

// ---------------------------------------------------------------------
// スカラー場の計算 (格子点ごとに 1 スレッド)
// ---------------------------------------------------------------------
[numthreads(256, 1, 1)]
void CS_Field(uint3 id : SV_DispatchThreadID)
{
    uint idx = id.x;
    if (idx >= mcNumVerts) return;

    float3 x = mcOrigin + (float3)VertexCoord(idx) * mcCell;
    int3   c = CellCoord(x);
    float  sum = 0.0;

    // SPH 格子 (セル幅 = h ≥ R) の 27 近傍を走査
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
            float3 r  = x - gPos[j].xyz;
            float  q2 = dot(r, r) * fieldInvR2;
            if (q2 < 1.0)
            {
                float t = 1.0 - q2;
                sum += t * t * t;
            }
        }
    }
    gFieldRW[idx] = sum;
}

// ---------------------------------------------------------------------
// マーチングキューブ法 (セルごとに 1 スレッド)
// ---------------------------------------------------------------------
static const int2 EDGE_VERTS[12] =
{
    int2(0,1), int2(2,3), int2(4,5), int2(6,7),   // x 方向
    int2(0,2), int2(1,3), int2(4,6), int2(5,7),   // y 方向
    int2(0,4), int2(1,5), int2(2,6), int2(3,7),   // z 方向
};

// 格子点の値 (範囲外は 0 = 流体の外)
float FieldAt(int3 c)
{
    if (any(c < 0) || any(c >= int3(mcDim))) return 0.0;
    return gField[VertexIndex((uint3)c)];
}

// 中心差分による勾配 → 法線は勾配の逆向き (値が減る方向 = 流体の外向き)
float3 NormalAt(int3 c)
{
    float3 g;
    g.x = FieldAt(c + int3(1, 0, 0)) - FieldAt(c - int3(1, 0, 0));
    g.y = FieldAt(c + int3(0, 1, 0)) - FieldAt(c - int3(0, 1, 0));
    g.z = FieldAt(c + int3(0, 0, 1)) - FieldAt(c - int3(0, 0, 1));
    return -g;
}

// ---------------------------------------------------------------------
// スカラー場の平滑化 (中心 2 + 6 近傍 1 の 7 点フィルタ)。gField → gFieldRW
// ---------------------------------------------------------------------
[numthreads(256, 1, 1)]
void CS_Smooth(uint3 id : SV_DispatchThreadID)
{
    uint idx = id.x;
    if (idx >= mcNumVerts) return;
    int3 c = (int3)VertexCoord(idx);
    float sum = 2.0 * gField[idx];
    sum += FieldAt(c + int3(1, 0, 0)) + FieldAt(c - int3(1, 0, 0));
    sum += FieldAt(c + int3(0, 1, 0)) + FieldAt(c - int3(0, 1, 0));
    sum += FieldAt(c + int3(0, 0, 1)) + FieldAt(c - int3(0, 0, 1));
    gFieldRW[idx] = sum / 8.0;
}

[numthreads(256, 1, 1)]
void CS_MarchingCubes(uint3 id : SV_DispatchThreadID)
{
    uint3 cells = mcDim - 1;
    uint  numCellsMC = cells.x * cells.y * cells.z;
    uint  idx = id.x;
    if (idx >= numCellsMC) return;

    int3 c;
    c.x = idx % cells.x;
    c.y = (idx / cells.x) % cells.y;
    c.z = idx / (cells.x * cells.y);

    // 8 頂点の値と内外パターン
    float v[8];
    uint  mask = 0;
    [unroll]
    for (int k = 0; k < 8; ++k)
    {
        int3 cc = c + int3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
        v[k] = gField[VertexIndex((uint3)cc)];
        if (v[k] > isoLevel) mask |= (1u << k);
    }
    if (mask == 0 || mask == 255) return;   // 等値面を含まない

    // 各エッジ上の交点 (位置・法線) を補間
    float3 ep[12], en[12];
    [unroll]
    for (int e = 0; e < 12; ++e)
    {
        int a = EDGE_VERTS[e].x, b = EDGE_VERTS[e].y;
        ep[e] = 0; en[e] = float3(0, 1, 0);
        if (((mask >> a) & 1) != ((mask >> b) & 1))
        {
            int3 ca = c + int3(a & 1, (a >> 1) & 1, (a >> 2) & 1);
            int3 cb = c + int3(b & 1, (b >> 1) & 1, (b >> 2) & 1);
            float t = saturate((isoLevel - v[a]) / (v[b] - v[a]));
            ep[e] = mcOrigin + lerp((float3)ca, (float3)cb, t) * mcCell;
            en[e] = normalize(lerp(NormalAt(ca), NormalAt(cb), t) + 1e-9);
        }
    }

    // 三角形テーブルを引いて出力
    uint base = mask * mcTableStride;
    for (uint k2 = 0; k2 + 2 < mcTableStride; k2 += 3)
    {
        int i0 = gTriTable[base + k2];
        if (i0 < 0) break;
        int i1 = gTriTable[base + k2 + 1];
        int i2 = gTriTable[base + k2 + 2];
        MCTriangle tri;
        tri.v0.p = ep[i0]; tri.v0.n = en[i0];
        tri.v1.p = ep[i1]; tri.v1.n = en[i1];
        tri.v2.p = ep[i2]; tri.v2.n = en[i2];
        gTriangles.Append(tri);
    }
}

// ---------------------------------------------------------------------
// 三角形数 → 描画引数 (頂点数 = 三角形数 * 3)
// ---------------------------------------------------------------------
[numthreads(1, 1, 1)]
void CS_MakeArgs(uint3 id : SV_DispatchThreadID)
{
    // 容量を超えた Append は破棄されているので、描画数も容量でクランプする
    uint triCount = min(gTriCount.Load(0), mcMaxTriangles);
    gDrawArgs.Store4(0, uint4(triCount * 3, 1, 0, 0));
}
