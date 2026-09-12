// =====================================================================
//  Renderer.cpp
//  シーン描画の実装 (詳細は Renderer.h)
// =====================================================================
#include "Renderer.h"
#include "SimConfig.h"
#include <cmath>

using namespace DirectX;

// ---------------------------------------------------------------------
// カメラ
// ---------------------------------------------------------------------
XMVECTOR OrbitCamera::Eye() const
{
    float yaw = XMConvertToRadians(yawDeg), pitch = XMConvertToRadians(pitchDeg);
    XMFLOAT3 offset(std::sin(yaw) * std::cos(pitch) * distance,
                    std::sin(pitch) * distance,
                    std::cos(yaw) * std::cos(pitch) * distance);
    return XMVectorSet(target.x + offset.x, target.y + offset.y, target.z + offset.z, 1.0f);
}

XMMATRIX OrbitCamera::View() const
{
    return XMMatrixLookAtLH(Eye(), XMLoadFloat3(&target), XMVectorSet(0, 1, 0, 0));
}

XMMATRIX OrbitCamera::Proj(float aspect, float nearZ, float farZ) const
{
    return XMMatrixPerspectiveFovLH(XMConvertToRadians(fovDeg), aspect, nearZ, farZ);
}

// ---------------------------------------------------------------------
// 初期化
// ---------------------------------------------------------------------
void Renderer::Initialize(D3DContext& d3d)
{
    m_d3d = &d3d;
    m_rtW = d3d.Width()  * cfg::SSAA;
    m_rtH = d3d.Height() * cfg::SSAA;

    // 動画の構図に合わせたカメラ
    m_camera.target   = XMFLOAT3(cfg::CAM_TARGET[0], cfg::CAM_TARGET[1], cfg::CAM_TARGET[2]);
    m_camera.yawDeg   = cfg::CAM_YAW_DEG;
    m_camera.pitchDeg = cfg::CAM_PITCH_DEG;
    m_camera.distance = cfg::CAM_DISTANCE;
    m_camera.fovDeg   = cfg::CAM_FOV_DEG;

    CreateTargets();
    CreateMeshes();
    CreateStates();

    // シェーダー
    ComPtr<ID3DBlob> meshBlob;
    m_vsMesh        = d3d.CreateVS(L"scene.hlsl", "VS_Mesh", &meshBlob);
    m_psFloor       = d3d.CreatePS(L"scene.hlsl", "PS_Floor");
    m_psGlass       = d3d.CreatePS(L"scene.hlsl", "PS_Glass");
    m_vsWater       = d3d.CreateVS(L"water.hlsl", "VS_Water");
    m_vsWaterShadow = d3d.CreateVS(L"water.hlsl", "VS_WaterShadow");
    m_vsWaterDepth  = d3d.CreateVS(L"water.hlsl", "VS_WaterDepth");
    m_psWater       = d3d.CreatePS(L"water.hlsl", "PS_Water");
    m_vsParticles   = d3d.CreateVS(L"water.hlsl", "VS_Particles");
    m_psParticles   = d3d.CreatePS(L"water.hlsl", "PS_Particles");
    m_vsFullscreen  = d3d.CreateVS(L"post.hlsl", "VS_Fullscreen");
    m_psPost        = d3d.CreatePS(L"post.hlsl", "PS_Post");

    // メッシュ用入力レイアウト (位置 + 法線)
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    ThrowIfFailed(d3d.Device()->CreateInputLayout(layout, 2, meshBlob->GetBufferPointer(), meshBlob->GetBufferSize(), &m_meshLayout), "CreateInputLayout");

    m_frameCB = d3d.CreateConstantBuffer(sizeof(FrameParams));
}

void Renderer::CreateTargets()
{
    ID3D11Device* dev = m_d3d->Device();

    // HDR カラー (内部解像度)
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = m_rtW; td.Height = m_rtH; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.SampleDesc = { 1, 0 };
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ThrowIfFailed(dev->CreateTexture2D(&td, nullptr, &m_hdrTex), "CreateTexture2D(hdr)");
    ThrowIfFailed(dev->CreateRenderTargetView(m_hdrTex.Get(), nullptr, &m_hdrRTV), "CreateRTV(hdr)");
    ThrowIfFailed(dev->CreateShaderResourceView(m_hdrTex.Get(), nullptr, &m_hdrSRV), "CreateSRV(hdr)");

    // 屈折参照用のコピー
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ThrowIfFailed(dev->CreateTexture2D(&td, nullptr, &m_sceneColorCopy), "CreateTexture2D(sceneCopy)");
    ThrowIfFailed(dev->CreateShaderResourceView(m_sceneColorCopy.Get(), nullptr, &m_sceneColorSRV), "CreateSRV(sceneCopy)");

    // 深度 (TYPELESS で作り DSV/SRV 両用)
    auto makeDepth = [&](int w, int h, ComPtr<ID3D11Texture2D>& tex, ComPtr<ID3D11DepthStencilView>* dsv, ComPtr<ID3D11ShaderResourceView>& srv)
    {
        D3D11_TEXTURE2D_DESC dd = {};
        dd.Width = w; dd.Height = h; dd.MipLevels = 1; dd.ArraySize = 1;
        dd.Format = DXGI_FORMAT_R32_TYPELESS;
        dd.SampleDesc = { 1, 0 };
        dd.Usage = D3D11_USAGE_DEFAULT;
        dd.BindFlags = D3D11_BIND_SHADER_RESOURCE | (dsv ? D3D11_BIND_DEPTH_STENCIL : 0);
        ThrowIfFailed(dev->CreateTexture2D(&dd, nullptr, &tex), "CreateTexture2D(depth)");
        if (dsv)
        {
            D3D11_DEPTH_STENCIL_VIEW_DESC dv = {};
            dv.Format = DXGI_FORMAT_D32_FLOAT;
            dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            ThrowIfFailed(dev->CreateDepthStencilView(tex.Get(), &dv, dsv->GetAddressOf()), "CreateDSV");
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_R32_FLOAT;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 1;
        ThrowIfFailed(dev->CreateShaderResourceView(tex.Get(), &sv, &srv), "CreateSRV(depth)");
    };
    makeDepth(m_rtW, m_rtH, m_depthTex, &m_depthDSV, m_depthSRV);               // メイン深度 (SRV は未使用)
    makeDepth(m_rtW, m_rtH, m_sceneDepthCopy, nullptr, m_sceneDepthSRV);        // 深度コピー (屈折用)
    makeDepth(SHADOW_SIZE, SHADOW_SIZE, m_shadowTex, &m_shadowDSV, m_shadowSRV); // シャドウマップ
    makeDepth(m_rtW, m_rtH, m_waterBackTex, &m_waterBackDSV, m_waterBackSRV);    // 水の裏面深度
}

// 直方体を 12 三角形として追加 (法線は外向き、頂点順は外から見て時計回り = D3D の表面)
void Renderer::AppendBox(std::vector<MeshVertex>& out, const XMFLOAT3& mn, const XMFLOAT3& mx)
{
    // 各面: 法線と 4 頂点
    struct Face { XMFLOAT3 n; XMFLOAT3 v[4]; };
    Face faces[6] = {
        { { 0, 0,-1 }, { {mn.x,mn.y,mn.z}, {mn.x,mx.y,mn.z}, {mx.x,mx.y,mn.z}, {mx.x,mn.y,mn.z} } },
        { { 0, 0, 1 }, { {mn.x,mn.y,mx.z}, {mx.x,mn.y,mx.z}, {mx.x,mx.y,mx.z}, {mn.x,mx.y,mx.z} } },
        { {-1, 0, 0 }, { {mn.x,mn.y,mn.z}, {mn.x,mn.y,mx.z}, {mn.x,mx.y,mx.z}, {mn.x,mx.y,mn.z} } },
        { { 1, 0, 0 }, { {mx.x,mn.y,mn.z}, {mx.x,mx.y,mn.z}, {mx.x,mx.y,mx.z}, {mx.x,mn.y,mx.z} } },
        { { 0,-1, 0 }, { {mn.x,mn.y,mn.z}, {mx.x,mn.y,mn.z}, {mx.x,mn.y,mx.z}, {mn.x,mn.y,mx.z} } },
        { { 0, 1, 0 }, { {mn.x,mx.y,mn.z}, {mn.x,mx.y,mx.z}, {mx.x,mx.y,mx.z}, {mx.x,mx.y,mn.z} } },
    };
    for (const Face& f : faces)
    {
        // 頂点順の外積が法線と同じ向きになるよう必要なら反転する
        XMVECTOR p0 = XMLoadFloat3(&f.v[0]), p1 = XMLoadFloat3(&f.v[1]), p2 = XMLoadFloat3(&f.v[2]);
        XMVECTOR c = XMVector3Cross(XMVectorSubtract(p1, p0), XMVectorSubtract(p2, p0));
        bool flip = XMVectorGetX(XMVector3Dot(c, XMLoadFloat3(&f.n))) < 0.0f;
        int idx[6] = { 0, 1, 2, 0, 2, 3 };
        if (flip) { idx[1] = 2; idx[2] = 1; idx[4] = 3; idx[5] = 2; }
        for (int k = 0; k < 6; ++k) out.push_back({ f.v[idx[k]], f.n });
    }
}

void Renderer::CreateMeshes()
{
    // 床: 大きな板 (フォグで地平線を隠す)
    {
        std::vector<MeshVertex> v;
        const float s = 60.0f, y = cfg::FLOOR_Y;
        AppendBox(v, XMFLOAT3(-s, y - 0.01f, -s), XMFLOAT3(s, y, s));
        m_floor.vb = m_d3d->CreateVertexBuffer(v.data(), (UINT)(v.size() * sizeof(MeshVertex)));
        m_floor.vertexCount = (UINT)v.size();
    }
    // ガラス水槽: 4 枚の壁 + 底板
    {
        std::vector<MeshVertex> v;
        const float in = cfg::TANK_INNER_HALF, out = cfg::TANK_OUTER_HALF, h = cfg::GLASS_VISIBLE_HEIGHT;
        const float y0 = cfg::FLOOR_Y, y1 = cfg::TANK_FLOOR_Y;
        AppendBox(v, XMFLOAT3(-out, y0, -out), XMFLOAT3(out, y1, out));      // 底板
        AppendBox(v, XMFLOAT3(-out, y1, -out), XMFLOAT3(-in,  h,  out));     // -x 壁
        AppendBox(v, XMFLOAT3( in,  y1, -out), XMFLOAT3( out, h,  out));     // +x 壁
        AppendBox(v, XMFLOAT3(-in,  y1, -out), XMFLOAT3( in,  h, -in));      // -z 壁
        AppendBox(v, XMFLOAT3(-in,  y1,  in),  XMFLOAT3( in,  h,  out));     // +z 壁
        m_glass.vb = m_d3d->CreateVertexBuffer(v.data(), (UINT)(v.size() * sizeof(MeshVertex)));
        m_glass.vertexCount = (UINT)v.size();
    }
}

void Renderer::CreateStates()
{
    ID3D11Device* dev = m_d3d->Device();

    D3D11_RASTERIZER_DESC rs = {};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_BACK;
    rs.DepthClipEnable = TRUE;
    ThrowIfFailed(dev->CreateRasterizerState(&rs, &m_rsCullBack), "CreateRasterizerState");
    rs.CullMode = D3D11_CULL_FRONT;
    ThrowIfFailed(dev->CreateRasterizerState(&rs, &m_rsCullFront), "CreateRasterizerState");
    rs.CullMode = D3D11_CULL_NONE;
    ThrowIfFailed(dev->CreateRasterizerState(&rs, &m_rsCullNone), "CreateRasterizerState");
    rs.DepthBias = 4; rs.SlopeScaledDepthBias = 2.0f;   // シャドウマップ用のバイアス
    ThrowIfFailed(dev->CreateRasterizerState(&rs, &m_rsShadow), "CreateRasterizerState");

    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    ThrowIfFailed(dev->CreateDepthStencilState(&ds, &m_dsDefault), "CreateDepthStencilState");
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ThrowIfFailed(dev->CreateDepthStencilState(&ds, &m_dsNoWrite), "CreateDepthStencilState");
    ds.DepthEnable = FALSE;
    ThrowIfFailed(dev->CreateDepthStencilState(&ds, &m_dsDisabled), "CreateDepthStencilState");

    D3D11_BLEND_DESC bs = {};
    bs.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ThrowIfFailed(dev->CreateBlendState(&bs, &m_bsOpaque), "CreateBlendState");
    bs.RenderTarget[0].BlendEnable = TRUE;              // 乗算済みアルファ
    bs.RenderTarget[0].SrcBlend  = D3D11_BLEND_ONE;
    bs.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bs.RenderTarget[0].BlendOp   = D3D11_BLEND_OP_ADD;
    bs.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
    bs.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bs.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
    ThrowIfFailed(dev->CreateBlendState(&bs, &m_bsPremultiplied), "CreateBlendState");

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ThrowIfFailed(dev->CreateSamplerState(&sd, &m_sampLinear), "CreateSamplerState");
    sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sd.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    sd.AddressU = sd.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1.0f;
    ThrowIfFailed(dev->CreateSamplerState(&sd, &m_sampShadow), "CreateSamplerState(shadow)");
}

// ---------------------------------------------------------------------
// フレーム定数の更新
// ---------------------------------------------------------------------
void Renderer::UpdateFrameParams(float time)
{
    const float nearZ = 0.05f, farZ = 60.0f;
    XMMATRIX view = m_camera.View();
    XMMATRIX proj = m_camera.Proj(float(m_rtW) / float(m_rtH), nearZ, farZ);

    // 光源: 平行光。シーン全体を覆う正射影
    XMVECTOR L = XMVector3Normalize(XMVectorSet(cfg::LIGHT_DIR[0], cfg::LIGHT_DIR[1], cfg::LIGHT_DIR[2], 0));
    XMVECTOR focus = XMVectorSet(0, 0.4f, 0, 1);
    XMMATRIX lightView = XMMatrixLookAtLH(XMVectorAdd(focus, XMVectorScale(L, 4.0f)), focus, XMVectorSet(0, 1, 0, 0));
    XMMATRIX lightProj = XMMatrixOrthographicLH(3.0f, 3.0f, 0.1f, 10.0f);

    // HLSL 側は mul(vec, M) の行ベクトル規約 + 列優先パッキングなので転置して格納
    XMStoreFloat4x4(&m_frame.viewProj,      XMMatrixTranspose(view * proj));
    XMStoreFloat4x4(&m_frame.view,          XMMatrixTranspose(view));
    XMStoreFloat4x4(&m_frame.lightViewProj, XMMatrixTranspose(lightView * lightProj));
    XMStoreFloat4(&m_frame.cameraPos, m_camera.Eye());
    XMStoreFloat4(&m_frame.lightDir, L);
    m_frame.screenSize = XMFLOAT4(float(m_rtW), float(m_rtH), 1.0f / m_rtW, 1.0f / m_rtH);
    m_frame.params     = XMFLOAT4(time, nearZ, farZ, 0.0f);
    m_frame.tank       = XMFLOAT4(cfg::TANK_INNER_HALF, cfg::TANK_OUTER_HALF, cfg::GLASS_VISIBLE_HEIGHT, cfg::FLOOR_Y);
    m_d3d->UpdateConstantBuffer(m_frameCB.Get(), &m_frame, sizeof(FrameParams));
}

// ---------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------
void Renderer::Render(SPHSimulator& sim, MarchingCubes& mc, float time)
{
    ID3D11DeviceContext* ctx = m_d3d->Context();
    UpdateFrameParams(time);

    ID3D11Buffer* cb = m_frameCB.Get();
    ctx->VSSetConstantBuffers(0, 1, &cb);
    ctx->PSSetConstantBuffers(0, 1, &cb);
    ID3D11SamplerState* samplers[2] = { m_sampLinear.Get(), m_sampShadow.Get() };
    ctx->PSSetSamplers(0, 2, samplers);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const float blendFactor[4] = { 0, 0, 0, 0 };

    ID3D11ShaderResourceView* nullSRVs[8] = {};
    auto unbindSRVs = [&]() { ctx->PSSetShaderResources(0, 8, nullSRVs); ctx->VSSetShaderResources(0, 8, nullSRVs); };

    // ---------------- 1. シャドウマップ ----------------
    {
        D3D11_VIEWPORT vp = { 0, 0, float(SHADOW_SIZE), float(SHADOW_SIZE), 0, 1 };
        ctx->RSSetViewports(1, &vp);
        ctx->OMSetRenderTargets(0, nullptr, m_shadowDSV.Get());
        ctx->ClearDepthStencilView(m_shadowDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        ctx->OMSetDepthStencilState(m_dsDefault.Get(), 0);
        ctx->OMSetBlendState(m_bsOpaque.Get(), blendFactor, 0xffffffff);
        ctx->RSSetState(m_rsShadow.Get());
        if (showWater)
        {
            ctx->IASetInputLayout(nullptr);
            ctx->VSSetShader(m_vsWaterShadow.Get(), nullptr, 0);
            ctx->PSSetShader(nullptr, nullptr, 0);
            ID3D11ShaderResourceView* tri = mc.TriangleSRV();
            ctx->VSSetShaderResources(0, 1, &tri);
            ctx->DrawInstancedIndirect(mc.DrawArgs(), 0);
        }
        unbindSRVs();
    }

    // ---------------- 2. 不透明パス (床) ----------------
    D3D11_VIEWPORT vp = { 0, 0, float(m_rtW), float(m_rtH), 0, 1 };
    ctx->RSSetViewports(1, &vp);
    ID3D11RenderTargetView* rtv = m_hdrRTV.Get();
    ctx->OMSetRenderTargets(1, &rtv, m_depthDSV.Get());
    const float bg[4] = { 0.27f, 0.27f, 0.28f, 1.0f };
    ctx->ClearRenderTargetView(rtv, bg);
    ctx->ClearDepthStencilView(m_depthDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx->RSSetState(m_rsCullBack.Get());
    {
        ctx->IASetInputLayout(m_meshLayout.Get());
        UINT stride = sizeof(MeshVertex), offset = 0;
        ctx->IASetVertexBuffers(0, 1, m_floor.vb.GetAddressOf(), &stride, &offset);
        ctx->VSSetShader(m_vsMesh.Get(), nullptr, 0);
        ctx->PSSetShader(m_psFloor.Get(), nullptr, 0);
        ID3D11ShaderResourceView* shadow = m_shadowSRV.Get();
        ctx->PSSetShaderResources(3, 1, &shadow);
        ctx->Draw(m_floor.vertexCount, 0);
        unbindSRVs();
    }

    // ---------------- 3. 屈折参照用コピー ----------------
    ctx->CopyResource(m_sceneColorCopy.Get(), m_hdrTex.Get());
    ctx->CopyResource(m_sceneDepthCopy.Get(), m_depthTex.Get());

    // ---------------- 3'. 水の裏面深度 (厚み推定用) ----------------
    if (showWater)
    {
        ctx->OMSetRenderTargets(0, nullptr, m_waterBackDSV.Get());
        ctx->ClearDepthStencilView(m_waterBackDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        ctx->RSSetState(m_rsCullFront.Get());          // 表面を捨てて裏面だけ描く
        ctx->IASetInputLayout(nullptr);
        ctx->VSSetShader(m_vsWaterDepth.Get(), nullptr, 0);
        ctx->PSSetShader(nullptr, nullptr, 0);
        ID3D11ShaderResourceView* tri = mc.TriangleSRV();
        ctx->VSSetShaderResources(0, 1, &tri);
        ctx->DrawInstancedIndirect(mc.DrawArgs(), 0);
        unbindSRVs();
        ctx->RSSetState(m_rsCullBack.Get());
        ctx->OMSetRenderTargets(1, &rtv, m_depthDSV.Get());
    }

    // ---------------- 4. 水面 ----------------
    if (showWater)
    {
        ctx->IASetInputLayout(nullptr);
        ctx->VSSetShader(m_vsWater.Get(), nullptr, 0);
        ctx->PSSetShader(m_psWater.Get(), nullptr, 0);
        ID3D11ShaderResourceView* tri = mc.TriangleSRV();
        ctx->VSSetShaderResources(0, 1, &tri);
        ID3D11ShaderResourceView* psRes[5] = { tri, m_sceneColorSRV.Get(), m_sceneDepthSRV.Get(), m_shadowSRV.Get(), m_waterBackSRV.Get() };
        ctx->PSSetShaderResources(0, 5, psRes);
        ctx->DrawInstancedIndirect(mc.DrawArgs(), 0);
        unbindSRVs();
    }

    // ---------------- 5. ガラス水槽 (半透明) ----------------
    if (showGlass)
    {
        ctx->IASetInputLayout(m_meshLayout.Get());
        UINT stride = sizeof(MeshVertex), offset = 0;
        ctx->IASetVertexBuffers(0, 1, m_glass.vb.GetAddressOf(), &stride, &offset);
        ctx->VSSetShader(m_vsMesh.Get(), nullptr, 0);
        ctx->PSSetShader(m_psGlass.Get(), nullptr, 0);
        ctx->RSSetState(m_rsCullNone.Get());
        ctx->OMSetDepthStencilState(m_dsNoWrite.Get(), 0);
        ctx->OMSetBlendState(m_bsPremultiplied.Get(), blendFactor, 0xffffffff);
        ctx->Draw(m_glass.vertexCount, 0);
        ctx->OMSetBlendState(m_bsOpaque.Get(), blendFactor, 0xffffffff);
        ctx->OMSetDepthStencilState(m_dsDefault.Get(), 0);
        ctx->RSSetState(m_rsCullBack.Get());
    }

    // ---------------- デバッグ: 粒子表示 ----------------
    if (showParticles)
    {
        ctx->IASetInputLayout(nullptr);
        ctx->VSSetShader(m_vsParticles.Get(), nullptr, 0);
        ctx->PSSetShader(m_psParticles.Get(), nullptr, 0);
        ID3D11ShaderResourceView* pos = sim.PositionSRV();
        ctx->VSSetShaderResources(4, 1, &pos);
        ctx->RSSetState(m_rsCullNone.Get());
        ctx->Draw(sim.NumParticles() * 6, 0);
        ctx->RSSetState(m_rsCullBack.Get());
        unbindSRVs();
    }

    // ---------------- 6. ポストプロセス → バックバッファ ----------------
    {
        D3D11_VIEWPORT vpBack = { 0, 0, float(m_d3d->Width()), float(m_d3d->Height()), 0, 1 };
        ctx->RSSetViewports(1, &vpBack);
        ID3D11RenderTargetView* back = m_d3d->BackBufferRTV();
        ctx->OMSetRenderTargets(1, &back, nullptr);
        ctx->OMSetDepthStencilState(m_dsDisabled.Get(), 0);
        ctx->IASetInputLayout(nullptr);
        ctx->VSSetShader(m_vsFullscreen.Get(), nullptr, 0);
        ctx->PSSetShader(m_psPost.Get(), nullptr, 0);
        ID3D11ShaderResourceView* hdr = m_hdrSRV.Get();
        ctx->PSSetShaderResources(0, 1, &hdr);
        ctx->Draw(3, 0);
        unbindSRVs();
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
    }
}
