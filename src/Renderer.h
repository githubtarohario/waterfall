#pragma once
// =====================================================================
//  Renderer.h
//  シーン描画。動画と同じ「グレーのスタジオ + ガラス水槽 + 水」を再現する。
//
//  パス構成:
//    1. シャドウマップ (光源視点から水面メッシュの深度)
//    2. 不透明パス      : 床 (影付き) を HDR ターゲットへ
//    3. コピー          : 屈折参照用に色と深度を複製
//    3'. 裏面深度       : 水の厚み推定用に MC メッシュの裏面深度を描く
//    4. 水面パス        : MC メッシュ (反射 + 屈折 + 吸収 + ハイライト)
//    5. ガラスパス      : 水槽 (半透明)
//    6. ポストプロセス  : SSAA 縮小 + トーンマップ + 周辺減光 → バックバッファ
// =====================================================================
#include "D3DUtil.h"
#include "SPHSimulator.h"
#include "MarchingCubes.h"

// shaders/render_common.hlsli の cbuffer FrameParams と一致させる
struct alignas(16) FrameParams
{
    DirectX::XMFLOAT4X4 viewProj;
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 lightViewProj;
    DirectX::XMFLOAT4   cameraPos;
    DirectX::XMFLOAT4   lightDir;
    DirectX::XMFLOAT4   screenSize;
    DirectX::XMFLOAT4   params;
    DirectX::XMFLOAT4   tank;
};

// 注視点を中心に回転するカメラ
struct OrbitCamera
{
    DirectX::XMFLOAT3 target;
    float yawDeg, pitchDeg, distance, fovDeg;
    DirectX::XMVECTOR Eye() const;
    DirectX::XMMATRIX View() const;
    DirectX::XMMATRIX Proj(float aspect, float nearZ, float farZ) const;
};

class Renderer
{
public:
    void Initialize(D3DContext& d3d);

    // sim/mc の現在の状態を描画してバックバッファへ出力する
    void Render(SPHSimulator& sim, MarchingCubes& mc, float time);

    OrbitCamera& Camera() { return m_camera; }
    bool showParticles = false;   // デバッグ: 粒子を点で表示
    bool showWater     = true;    // 水面メッシュの表示
    bool showGlass     = true;    // ガラス水槽の表示

private:
    void CreateTargets();
    void CreateMeshes();
    void CreateStates();
    void UpdateFrameParams(float time);

    struct MeshVertex { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT3 normal; };
    struct Mesh { ComPtr<ID3D11Buffer> vb; UINT vertexCount = 0; };
    static void AppendBox(std::vector<MeshVertex>& out, const DirectX::XMFLOAT3& mn, const DirectX::XMFLOAT3& mx);

    D3DContext* m_d3d = nullptr;
    OrbitCamera m_camera = {};
    int m_rtW = 0, m_rtH = 0;          // 内部描画解像度 (SSAA 後)
    static constexpr int SHADOW_SIZE = 2048;

    // レンダーターゲット
    ComPtr<ID3D11Texture2D>          m_hdrTex, m_depthTex, m_sceneColorCopy, m_sceneDepthCopy, m_shadowTex, m_waterBackTex;
    ComPtr<ID3D11RenderTargetView>   m_hdrRTV;
    ComPtr<ID3D11DepthStencilView>   m_depthDSV, m_shadowDSV, m_waterBackDSV;
    ComPtr<ID3D11ShaderResourceView> m_hdrSRV, m_depthSRV, m_sceneColorSRV, m_sceneDepthSRV, m_shadowSRV, m_waterBackSRV;

    // メッシュ
    Mesh m_floor, m_glass;
    ComPtr<ID3D11InputLayout> m_meshLayout;

    // シェーダー
    ComPtr<ID3D11VertexShader> m_vsMesh, m_vsWater, m_vsWaterShadow, m_vsWaterDepth, m_vsParticles, m_vsFullscreen;
    ComPtr<ID3D11PixelShader>  m_psFloor, m_psGlass, m_psWater, m_psParticles, m_psPost;

    // ステート
    ComPtr<ID3D11RasterizerState>   m_rsCullBack, m_rsCullFront, m_rsCullNone, m_rsShadow;
    ComPtr<ID3D11DepthStencilState> m_dsDefault, m_dsNoWrite, m_dsDisabled;
    ComPtr<ID3D11BlendState>        m_bsOpaque, m_bsPremultiplied;
    ComPtr<ID3D11SamplerState>      m_sampLinear, m_sampShadow;
    ComPtr<ID3D11Buffer>            m_frameCB;
    FrameParams                     m_frame = {};
};
