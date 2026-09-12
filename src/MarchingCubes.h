#pragma once
// =====================================================================
//  MarchingCubes.h
//  GPU マーチングキューブ法による水面メッシュ生成。
//    1. 粒子からスカラー場を格子点上に計算 (CS_Field)
//    2. セルごとに等値面を三角形化して AppendStructuredBuffer へ (CS_MarchingCubes)
//    3. 三角形数を DrawInstancedIndirect の引数へ変換 (CS_MakeArgs)
// =====================================================================
#include "D3DUtil.h"
#include "SPHSimulator.h"

// shaders/mc.hlsl の cbuffer MCParams と一致させる
struct alignas(16) MCParams
{
    float    origin[3];  float cell;
    uint32_t dim[3];     uint32_t numVerts;
    float    isoLevel;   float fieldRadius;
    float    fieldInvR2; uint32_t tableStride;
    uint32_t maxTriangles; float pad[3];
};

class MarchingCubes
{
public:
    // spacing: 粒子間隔 (格子間隔とカーネル半径の基準)
    void Initialize(D3DContext& d3d, float spacing);
    // 現在の粒子位置からメッシュを生成する (SPH の格子が最新である必要あり)
    void Build(SPHSimulator& sim);

    // 描画用
    ID3D11ShaderResourceView* TriangleSRV() const { return m_triangles.srv.Get(); }
    ID3D11Buffer*             DrawArgs()    const { return m_drawArgs.buffer.Get(); }
    const MCParams&           Params()      const { return m_params; }

    // 直近の三角形数 (デバッグ表示用。GPU から読み戻すため 1 フレーム遅れる)
    uint32_t LastTriangleCount();

private:
    D3DContext* m_d3d = nullptr;
    MCParams    m_params = {};

    ComPtr<ID3D11Buffer> m_paramsCB;
    GpuBuffer m_field;       // スカラー場
    GpuBuffer m_fieldSmooth; // 平滑化後のスカラー場
    GpuBuffer m_triTable;    // 三角形テーブル
    GpuBuffer m_triangles;   // 出力三角形 (Append)
    GpuBuffer m_triCount;    // 三角形数 (CopyStructureCount の書き込み先)
    GpuBuffer m_drawArgs;    // 描画引数
    ComPtr<ID3D11Buffer> m_readback;   // 三角形数の CPU 読み戻し用

    ComPtr<ID3D11ComputeShader> m_csField, m_csSmooth, m_csCubes, m_csArgs;
};
