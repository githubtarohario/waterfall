#pragma once
// =====================================================================
//  SPHSimulator.h
//  GPU (DirectX 11 コンピュートシェーダー) による WCSPH 流体シミュレーション。
//
//  - 一様格子 + カウンティングソートで近傍探索 (毎サブステップ粒子を並べ替え)
//  - Tait 状態方程式による弱圧縮性 SPH (Müller 2003 のカーネル群)
//  - XSPH による速度平滑化、簡易的な境界密度補正
//  - 水槽 (ガラス箱) / 床 / 領域境界との衝突処理
// =====================================================================
#include "D3DUtil.h"
#include <cstdint>

// シェーダー sph_common.hlsli の cbuffer SimParams と一致させる
struct alignas(16) SimParams
{
    float    domainMin[3];  float cellSize;
    float    domainMax[3];  float h;
    uint32_t gridDim[3];    uint32_t numCells;
    float    h2;            float mass;
    float    restDensity;   float taitB;
    float    viscosity;     float xsphEps;
    float    dt;            float gravity;
    float    poly6;         float spikyGrad;
    float    viscLap;       float tankInner;
    float    tankOuter;     float tankHeight;
    float    tankFloorY;    float floorY;
    float    restitution;   float friction;
    float    maxSpeed;      float boundaryDensityScale;
    uint32_t numParticles;  float artificialAlpha; float cohesion; float soundSpeed;
};

class SPHSimulator
{
public:
    // spacing: 粒子間隔 [m]。SimConfig.h の水塊定義から粒子を生成する
    //   restTest=true で水塊 A を床に静置した安定性確認シーンにする
    void Initialize(D3DContext& d3d, float spacing, bool restTest = false);
    // 初期状態に戻す
    void Reset();
    // 格子の再構築と粒子の並べ替えのみ行う (MC の近傍探索で最新の格子が必要なとき)
    void SortParticles();
    // 1 サブステップ進める
    void Step();
    // 1 描画フレーム分 (FRAME_DT) 進める
    void StepFrame();

    uint32_t NumParticles() const { return m_params.numParticles; }
    uint32_t SubstepsPerFrame() const { return m_substepsPerFrame; }
    float    SubstepDt() const { return m_params.dt; }
    float    SimTime() const { return m_time; }
    const SimParams& Params() const { return m_params; }

    // 描画・MC 側が参照するリソース
    ID3D11Buffer*             ParamsCB()    const { return m_paramsCB.Get(); }
    ID3D11ShaderResourceView* PositionSRV() const { return m_pos[m_cur].srv.Get(); }
    ID3D11ShaderResourceView* CellStartSRV()const { return m_cellStart.srv.Get(); }

private:
    void BuildInitialParticles(float spacing, bool restTest);
    void UploadParams();

    D3DContext* m_d3d = nullptr;
    SimParams   m_params = {};
    uint32_t    m_substepsPerFrame = 1;
    float       m_time = 0.0f;
    int         m_cur = 0;   // ピンポンバッファの現在側

    std::vector<DirectX::XMFLOAT4> m_initialPos;   // Reset 用

    // GPU リソース
    ComPtr<ID3D11Buffer> m_paramsCB;
    GpuBuffer m_pos[2], m_vel[2];       // 位置・速度 (ピンポン)
    GpuBuffer m_xsph;                   // XSPH 補正速度
    GpuBuffer m_densPres;               // 密度・圧力
    GpuBuffer m_accel;                  // 加速度
    GpuBuffer m_cellCount, m_cellStart; // 格子
    GpuBuffer m_cellIndex, m_cellOffset;// 粒子ごとの所属セル

    ComPtr<ID3D11ComputeShader> m_csClear, m_csCount, m_csScan, m_csReorder,
                                m_csDensity, m_csForce, m_csIntegrate;
};
