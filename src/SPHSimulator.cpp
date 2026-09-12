// =====================================================================
//  SPHSimulator.cpp
//  GPU WCSPH の実装 (詳細は SPHSimulator.h / shaders/sph.hlsl)
// =====================================================================
#include "SPHSimulator.h"
#include "SimConfig.h"
#include <cmath>
#include <cstdio>
#include <random>

using namespace DirectX;

namespace
{
    constexpr float PI = 3.14159265358979f;
    constexpr UINT  THREADS = 256;
    inline UINT Groups(UINT n) { return (n + THREADS - 1) / THREADS; }
}

void SPHSimulator::Initialize(D3DContext& d3d, float spacing, bool restTest)
{
    m_d3d = &d3d;

    // ------------------------------------------------------------
    // 物理パラメータの導出
    // ------------------------------------------------------------
    const float h = spacing * cfg::H_PER_SPACING;
    SimParams& p = m_params;
    for (int k = 0; k < 3; ++k) { p.domainMin[k] = cfg::DOMAIN_MIN[k]; p.domainMax[k] = cfg::DOMAIN_MAX[k]; }
    p.cellSize = h;
    p.h        = h;
    p.h2       = h * h;
    for (int k = 0; k < 3; ++k)
        p.gridDim[k] = (uint32_t)std::ceil((cfg::DOMAIN_MAX[k] - cfg::DOMAIN_MIN[k]) / h);
    p.numCells = p.gridDim[0] * p.gridDim[1] * p.gridDim[2];

    p.restDensity = cfg::REST_DENSITY;
    p.taitB       = cfg::REST_DENSITY * cfg::SOUND_SPEED * cfg::SOUND_SPEED / cfg::TAIT_GAMMA;
    p.viscosity   = cfg::VISCOSITY_MU;
    p.xsphEps     = cfg::XSPH_EPS;
    p.gravity     = cfg::GRAVITY;
    p.artificialAlpha = cfg::ARTIFICIAL_VISC_ALPHA;
    p.cohesion    = cfg::COHESION_KAPPA;
    p.soundSpeed  = cfg::SOUND_SPEED;

    // カーネルの正規化定数
    p.poly6     = 315.0f / (64.0f * PI * std::pow(h, 9.0f));
    p.spikyGrad = -45.0f / (PI * std::pow(h, 6.0f));
    p.viscLap   =  45.0f / (PI * std::pow(h, 6.0f));

    // 粒子質量: 格子上に静止した内部粒子の密度が ρ0 になるよう決める
    {
        double sumW = 0.0;
        int n = (int)std::ceil(h / spacing);
        for (int z = -n; z <= n; ++z) for (int y = -n; y <= n; ++y) for (int x = -n; x <= n; ++x)
        {
            float r2 = (x * x + y * y + z * z) * spacing * spacing;
            if (r2 < h * h) { float t = h * h - r2; sumW += (double)p.poly6 * t * t * t; }
        }
        p.mass = (float)(cfg::REST_DENSITY / sumW);
    }

    // 時間刻み: CFL 条件 dt = CFL * h / c を満たす最大の dt でフレームを等分割
    float dtMax = cfg::CFL * h / cfg::SOUND_SPEED;
    m_substepsPerFrame = (uint32_t)std::ceil(cfg::FRAME_DT / dtMax);
    p.dt       = cfg::FRAME_DT / m_substepsPerFrame;
    p.maxSpeed = cfg::MAX_SPEED_PER_H * h / p.dt;

    p.tankInner   = cfg::TANK_INNER_HALF;
    p.tankOuter   = cfg::TANK_OUTER_HALF;
    p.tankHeight  = cfg::TANK_HEIGHT;
    p.tankFloorY  = cfg::TANK_FLOOR_Y;
    p.floorY      = cfg::FLOOR_Y;
    p.restitution = cfg::WALL_RESTITUTION;
    p.friction    = cfg::WALL_FRICTION;
    p.boundaryDensityScale = cfg::BOUNDARY_DENSITY_SCALE;

    // ------------------------------------------------------------
    // 粒子の初期配置
    // ------------------------------------------------------------
    BuildInitialParticles(spacing, restTest);
    p.numParticles = (uint32_t)m_initialPos.size();

    std::printf("[SPH] particles=%u  h=%.4f  mass=%.3e  dt=%.5f  substeps/frame=%u  grid=%ux%ux%u (%u cells)\n",
                p.numParticles, h, p.mass, p.dt, m_substepsPerFrame, p.gridDim[0], p.gridDim[1], p.gridDim[2], p.numCells);

    // ------------------------------------------------------------
    // GPU リソース
    // ------------------------------------------------------------
    const UINT N = p.numParticles;
    std::vector<XMFLOAT4> zeros(N, XMFLOAT4(0, 0, 0, 0));
    m_pos[0] = d3d.CreateStructured(sizeof(XMFLOAT4), N, m_initialPos.data());
    m_pos[1] = d3d.CreateStructured(sizeof(XMFLOAT4), N, zeros.data());
    m_vel[0] = d3d.CreateStructured(sizeof(XMFLOAT4), N, zeros.data());
    m_vel[1] = d3d.CreateStructured(sizeof(XMFLOAT4), N, zeros.data());
    m_xsph   = d3d.CreateStructured(sizeof(XMFLOAT4), N, zeros.data());
    m_accel  = d3d.CreateStructured(sizeof(XMFLOAT4), N, zeros.data());
    m_densPres   = d3d.CreateStructured(sizeof(XMFLOAT2), N);
    m_cellIndex  = d3d.CreateStructured(sizeof(uint32_t), N);
    m_cellOffset = d3d.CreateStructured(sizeof(uint32_t), N);
    m_cellCount  = d3d.CreateStructured(sizeof(uint32_t), p.numCells);
    m_cellStart  = d3d.CreateStructured(sizeof(uint32_t), p.numCells + 1);

    m_paramsCB = d3d.CreateConstantBuffer(sizeof(SimParams));
    UploadParams();

    // シェーダー
    m_csClear     = d3d.CreateCS(L"sph.hlsl", "CS_ClearCells");
    m_csCount     = d3d.CreateCS(L"sph.hlsl", "CS_Count");
    m_csScan      = d3d.CreateCS(L"sph.hlsl", "CS_Scan");
    m_csReorder   = d3d.CreateCS(L"sph.hlsl", "CS_Reorder");
    m_csDensity   = d3d.CreateCS(L"sph.hlsl", "CS_Density");
    m_csForce     = d3d.CreateCS(L"sph.hlsl", "CS_Force");
    m_csIntegrate = d3d.CreateCS(L"sph.hlsl", "CS_Integrate");

    m_cur  = 0;
    m_time = 0.0f;
}

void SPHSimulator::BuildInitialParticles(float spacing, bool restTest)
{
    m_initialPos.clear();
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> jitter(-0.02f * spacing, 0.02f * spacing);

    // 各水塊を格子状に粒子で満たす (完全な対称性を崩すため微小な乱れを加える)
    for (int b = 0; b < cfg::NUM_WATER_BLOCKS; ++b)
    {
        cfg::WaterBlock blk = cfg::WATER_BLOCKS[b];
        if (restTest)
        {
            // 安定性テスト: 水塊 A だけを床の上に置く
            if (b != 0) continue;
            float dy = blk.mn[1] - cfg::TANK_FLOOR_Y - spacing * 0.5f;
            blk.mn[1] -= dy; blk.mx[1] -= dy;
        }
        int nx = (int)std::floor((blk.mx[0] - blk.mn[0]) / spacing);
        int ny = (int)std::floor((blk.mx[1] - blk.mn[1]) / spacing);
        int nz = (int)std::floor((blk.mx[2] - blk.mn[2]) / spacing);
        for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x)
        {
            m_initialPos.emplace_back(
                blk.mn[0] + (x + 0.5f) * spacing + jitter(rng),
                blk.mn[1] + (y + 0.5f) * spacing + jitter(rng),
                blk.mn[2] + (z + 0.5f) * spacing + jitter(rng),
                0.0f);
        }
    }
}

void SPHSimulator::UploadParams()
{
    m_d3d->UpdateConstantBuffer(m_paramsCB.Get(), &m_params, sizeof(SimParams));
}

void SPHSimulator::Reset()
{
    ID3D11DeviceContext* ctx = m_d3d->Context();
    std::vector<XMFLOAT4> zeros(m_params.numParticles, XMFLOAT4(0, 0, 0, 0));
    ctx->UpdateSubresource(m_pos[0].buffer.Get(), 0, nullptr, m_initialPos.data(), 0, 0);
    ctx->UpdateSubresource(m_vel[0].buffer.Get(), 0, nullptr, zeros.data(), 0, 0);
    m_cur  = 0;
    m_time = 0.0f;
}

void SPHSimulator::SortParticles()
{
    ID3D11DeviceContext* ctx = m_d3d->Context();
    const UINT N = m_params.numParticles;
    ID3D11Buffer* cb = m_paramsCB.Get();
    ctx->CSSetConstantBuffers(0, 1, &cb);

    auto setSRV = [&](UINT slot, ID3D11ShaderResourceView* v)  { ctx->CSSetShaderResources(slot, 1, &v); };
    auto setUAV = [&](UINT slot, ID3D11UnorderedAccessView* v) { ctx->CSSetUnorderedAccessViews(slot, 1, &v, nullptr); };

    // 1. セルカウンタのクリア
    ctx->CSSetShader(m_csClear.Get(), nullptr, 0);
    setUAV(0, m_cellCount.uav.Get());
    ctx->Dispatch(Groups(m_params.numCells), 1, 1);
    m_d3d->UnbindCompute();

    // 2. 所属セルのカウント
    ctx->CSSetShader(m_csCount.Get(), nullptr, 0);
    setSRV(0, m_pos[m_cur].srv.Get());
    setUAV(0, m_cellCount.uav.Get());
    setUAV(2, m_cellIndex.uav.Get());
    setUAV(3, m_cellOffset.uav.Get());
    ctx->Dispatch(Groups(N), 1, 1);
    m_d3d->UnbindCompute();

    // 3. 累積和
    ctx->CSSetShader(m_csScan.Get(), nullptr, 0);
    setUAV(0, m_cellCount.uav.Get());
    setUAV(1, m_cellStart.uav.Get());
    ctx->Dispatch(1, 1, 1);
    m_d3d->UnbindCompute();

    // 4. 並べ替え (cur → 1-cur)
    ctx->CSSetShader(m_csReorder.Get(), nullptr, 0);
    setSRV(0, m_pos[m_cur].srv.Get());
    setSRV(1, m_vel[m_cur].srv.Get());
    setSRV(2, m_cellStart.srv.Get());
    setSRV(6, m_cellIndex.srv.Get());
    setSRV(7, m_cellOffset.srv.Get());
    setUAV(4, m_pos[1 - m_cur].uav.Get());
    setUAV(5, m_vel[1 - m_cur].uav.Get());
    ctx->Dispatch(Groups(N), 1, 1);
    m_d3d->UnbindCompute();
    m_cur = 1 - m_cur;
}

void SPHSimulator::Step()
{
    // 格子構築と並べ替え (手順 1〜4)
    SortParticles();

    ID3D11DeviceContext* ctx = m_d3d->Context();
    const UINT N = m_params.numParticles;
    auto setSRV = [&](UINT slot, ID3D11ShaderResourceView* v)  { ctx->CSSetShaderResources(slot, 1, &v); };
    auto setUAV = [&](UINT slot, ID3D11UnorderedAccessView* v) { ctx->CSSetUnorderedAccessViews(slot, 1, &v, nullptr); };

    // 5. 密度・圧力
    ctx->CSSetShader(m_csDensity.Get(), nullptr, 0);
    setSRV(0, m_pos[m_cur].srv.Get());
    setSRV(2, m_cellStart.srv.Get());
    setUAV(6, m_densPres.uav.Get());
    ctx->Dispatch(Groups(N), 1, 1);
    m_d3d->UnbindCompute();

    // 6. 力 (加速度) と XSPH
    ctx->CSSetShader(m_csForce.Get(), nullptr, 0);
    setSRV(0, m_pos[m_cur].srv.Get());
    setSRV(1, m_vel[m_cur].srv.Get());
    setSRV(2, m_cellStart.srv.Get());
    setSRV(3, m_densPres.srv.Get());
    setUAV(5, m_xsph.uav.Get());     // u5 = XSPH 出力
    setUAV(7, m_accel.uav.Get());
    ctx->Dispatch(Groups(N), 1, 1);
    m_d3d->UnbindCompute();

    // 7. 時間積分 (cur → 1-cur)
    ctx->CSSetShader(m_csIntegrate.Get(), nullptr, 0);
    setSRV(0, m_pos[m_cur].srv.Get());
    setSRV(1, m_vel[m_cur].srv.Get());
    setSRV(4, m_accel.srv.Get());
    setSRV(5, m_xsph.srv.Get());
    setUAV(4, m_pos[1 - m_cur].uav.Get());
    setUAV(5, m_vel[1 - m_cur].uav.Get());
    ctx->Dispatch(Groups(N), 1, 1);
    m_d3d->UnbindCompute();
    m_cur = 1 - m_cur;

    m_time += m_params.dt;
}

void SPHSimulator::StepFrame()
{
    for (uint32_t i = 0; i < m_substepsPerFrame; ++i) Step();
}
