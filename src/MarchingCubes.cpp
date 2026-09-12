// =====================================================================
//  MarchingCubes.cpp
//  GPU マーチングキューブ法の実装 (詳細は MarchingCubes.h / shaders/mc.hlsl)
// =====================================================================
#include "MarchingCubes.h"
#include "MCTables.h"
#include "SimConfig.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    constexpr UINT THREADS = 256;
    inline UINT Groups(UINT n) { return (n + THREADS - 1) / THREADS; }
}

void MarchingCubes::Initialize(D3DContext& d3d, float spacing)
{
    m_d3d = &d3d;

    // ------------------------------------------------------------
    // 格子の設定: シミュレーション領域全体を覆う
    // ------------------------------------------------------------
    MCParams& p = m_params;
    p.cell = spacing * cfg::MC_CELL_PER_SPACING;
    for (int k = 0; k < 3; ++k)
    {
        p.origin[k] = cfg::DOMAIN_MIN[k] - p.cell;                    // 端に 1 セル余裕を持たせる
        p.dim[k]    = (uint32_t)std::ceil((cfg::DOMAIN_MAX[k] - cfg::DOMAIN_MIN[k]) / p.cell) + 3;
    }
    p.numVerts     = p.dim[0] * p.dim[1] * p.dim[2];
    p.isoLevel     = cfg::MC_ISO_LEVEL;
    p.fieldRadius  = spacing * cfg::MC_FIELD_RADIUS_PER_SPACING;
    p.fieldInvR2   = 1.0f / (p.fieldRadius * p.fieldRadius);
    p.tableStride  = mc::TABLE_STRIDE;
    p.maxTriangles = cfg::MC_MAX_TRIANGLES;

    std::printf("[MC] grid=%ux%ux%u (%u vertices)  cell=%.4f  R=%.4f  iso=%.2f\n",
                p.dim[0], p.dim[1], p.dim[2], p.numVerts, p.cell, p.fieldRadius, p.isoLevel);

    // ------------------------------------------------------------
    // 三角形テーブルを生成して検証
    // ------------------------------------------------------------
    std::vector<int32_t> table = mc::GenerateTriangleTable();
    if (!mc::SelfTest(table))
        throw std::runtime_error("Marching cubes table self-test failed");

    // ------------------------------------------------------------
    // GPU リソース
    // ------------------------------------------------------------
    m_field     = d3d.CreateStructured(sizeof(float), p.numVerts);
    m_fieldSmooth = d3d.CreateStructured(sizeof(float), p.numVerts);
    m_triTable  = d3d.CreateStructured(sizeof(int32_t), (UINT)table.size(), table.data());
    m_triangles = d3d.CreateStructured(sizeof(float) * 18, p.maxTriangles, nullptr, /*append=*/true);
    m_triCount  = d3d.CreateRaw(16);
    m_drawArgs  = d3d.CreateRaw(16, /*indirectArgs=*/true);
    m_paramsCB  = d3d.CreateConstantBuffer(sizeof(MCParams));
    d3d.UpdateConstantBuffer(m_paramsCB.Get(), &p, sizeof(MCParams));

    // 読み戻し用ステージングバッファ
    D3D11_BUFFER_DESC rb = {};
    rb.ByteWidth      = 16;
    rb.Usage          = D3D11_USAGE_STAGING;
    rb.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ThrowIfFailed(d3d.Device()->CreateBuffer(&rb, nullptr, &m_readback), "CreateBuffer(readback)");

    m_csField = d3d.CreateCS(L"mc.hlsl", "CS_Field");
    m_csSmooth = d3d.CreateCS(L"mc.hlsl", "CS_Smooth");
    m_csCubes = d3d.CreateCS(L"mc.hlsl", "CS_MarchingCubes");
    m_csArgs  = d3d.CreateCS(L"mc.hlsl", "CS_MakeArgs");
}

void MarchingCubes::Build(SPHSimulator& sim)
{
    ID3D11DeviceContext* ctx = m_d3d->Context();
    ID3D11Buffer* cbs[2] = { sim.ParamsCB(), m_paramsCB.Get() };
    ctx->CSSetConstantBuffers(0, 2, cbs);

    auto setSRV = [&](UINT slot, ID3D11ShaderResourceView* v) { ctx->CSSetShaderResources(slot, 1, &v); };

    // 1. スカラー場
    ctx->CSSetShader(m_csField.Get(), nullptr, 0);
    setSRV(0, sim.PositionSRV());
    setSRV(2, sim.CellStartSRV());
    ID3D11UnorderedAccessView* fieldUAV = m_field.uav.Get();
    ctx->CSSetUnorderedAccessViews(0, 1, &fieldUAV, nullptr);
    ctx->Dispatch(Groups(m_params.numVerts), 1, 1);
    m_d3d->UnbindCompute();

    // 1'. 平滑化 (field ⇄ fieldSmooth をピンポンしながら指定回数)
    GpuBuffer* src = &m_field;
    GpuBuffer* dst = &m_fieldSmooth;
    for (int pass = 0; pass < cfg::MC_SMOOTH_PASSES; ++pass)
    {
        ctx->CSSetShader(m_csSmooth.Get(), nullptr, 0);
        setSRV(3, src->srv.Get());
        ID3D11UnorderedAccessView* smoothUAV = dst->uav.Get();
        ctx->CSSetUnorderedAccessViews(0, 1, &smoothUAV, nullptr);
        ctx->Dispatch(Groups(m_params.numVerts), 1, 1);
        m_d3d->UnbindCompute();
        std::swap(src, dst);
    }
    ID3D11ShaderResourceView* mcField = src->srv.Get();

    // 2. マーチングキューブ (Append カウンタを 0 にリセットして開始)
    ctx->CSSetShader(m_csCubes.Get(), nullptr, 0);
    setSRV(3, mcField);
    setSRV(4, m_triTable.srv.Get());
    ID3D11UnorderedAccessView* triUAV = m_triangles.uav.Get();
    UINT initialCount = 0;
    ctx->CSSetUnorderedAccessViews(1, 1, &triUAV, &initialCount);
    UINT numCells = (m_params.dim[0] - 1) * (m_params.dim[1] - 1) * (m_params.dim[2] - 1);
    ctx->Dispatch(Groups(numCells), 1, 1);
    m_d3d->UnbindCompute();

    // 3. 三角形数 → 描画引数
    ctx->CopyStructureCount(m_triCount.buffer.Get(), 0, m_triangles.uav.Get());
    ctx->CSSetShader(m_csArgs.Get(), nullptr, 0);
    ID3D11UnorderedAccessView* uavs[2] = { m_triCount.uav.Get(), m_drawArgs.uav.Get() };
    ctx->CSSetUnorderedAccessViews(2, 2, uavs, nullptr);
    ctx->Dispatch(1, 1, 1);
    m_d3d->UnbindCompute();
}

uint32_t MarchingCubes::LastTriangleCount()
{
    ID3D11DeviceContext* ctx = m_d3d->Context();
    ctx->CopyResource(m_readback.Get(), m_drawArgs.buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(ctx->Map(m_readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 0;
    uint32_t verts = *(const uint32_t*)mapped.pData;
    ctx->Unmap(m_readback.Get(), 0);
    return verts / 3;
}
