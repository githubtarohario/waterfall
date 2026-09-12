// =====================================================================
//  D3DUtil.cpp
//  DirectX 11 ユーティリティの実装 (詳細は D3DUtil.h)
// =====================================================================
#include "D3DUtil.h"
#include <cstdio>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

void D3DContext::Initialize(HWND hwnd, int width, int height, bool debugLayer)
{
    m_width = width; m_height = height;

    // 実行ファイルの場所から shaders/ ディレクトリを決める
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir = exePath;
    dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
    m_shaderDir = dir + L"shaders\\";
    if (GetFileAttributesW((m_shaderDir + L"sph.hlsl").c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        // ビルドディレクトリから実行した場合は 1 つ上の shaders/ を探す
        m_shaderDir = dir + L"..\\shaders\\";
        if (GetFileAttributesW((m_shaderDir + L"sph.hlsl").c_str()) == INVALID_FILE_ATTRIBUTES)
            m_shaderDir = L"shaders\\";   // 最後はカレントディレクトリ
    }

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount       = 2;
    sd.BufferDesc.Width  = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate = { 60, 1 };
    sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow      = hwnd;
    sd.SampleDesc        = { 1, 0 };
    sd.Windowed          = TRUE;
    sd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
    if (debugLayer) flags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL got;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, 2, D3D11_SDK_VERSION, &sd, &m_swapChain, &m_device, &got, &m_context);
    if (FAILED(hr) && debugLayer)
    {
        // デバッグレイヤーが無い環境では通常モードで再試行
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, 2, D3D11_SDK_VERSION, &sd, &m_swapChain, &m_device, &got, &m_context);
    }
    ThrowIfFailed(hr, "D3D11CreateDeviceAndSwapChain");

    ThrowIfFailed(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&m_backBuffer)), "GetBuffer");
    ThrowIfFailed(m_device->CreateRenderTargetView(m_backBuffer.Get(), nullptr, &m_backBufferRTV), "CreateRenderTargetView");
}

GpuBuffer D3DContext::CreateStructured(UINT stride, UINT count, const void* initData, bool append)
{
    GpuBuffer b; b.count = count; b.stride = stride;
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth           = stride * count;
    desc.Usage               = D3D11_USAGE_DEFAULT;
    desc.BindFlags           = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = stride;
    D3D11_SUBRESOURCE_DATA init = { initData, 0, 0 };
    ThrowIfFailed(m_device->CreateBuffer(&desc, initData ? &init : nullptr, &b.buffer), "CreateBuffer(structured)");

    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format              = DXGI_FORMAT_UNKNOWN;
    srv.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
    srv.Buffer.NumElements  = count;
    ThrowIfFailed(m_device->CreateShaderResourceView(b.buffer.Get(), &srv, &b.srv), "CreateShaderResourceView");

    D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format             = DXGI_FORMAT_UNKNOWN;
    uav.ViewDimension      = D3D11_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = count;
    uav.Buffer.Flags       = append ? D3D11_BUFFER_UAV_FLAG_APPEND : 0;
    ThrowIfFailed(m_device->CreateUnorderedAccessView(b.buffer.Get(), &uav, &b.uav), "CreateUnorderedAccessView");
    return b;
}

GpuBuffer D3DContext::CreateRaw(UINT byteSize, bool indirectArgs)
{
    GpuBuffer b; b.count = byteSize / 4; b.stride = 4;
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = byteSize;
    desc.Usage     = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS | (indirectArgs ? D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS : 0);
    ThrowIfFailed(m_device->CreateBuffer(&desc, nullptr, &b.buffer), "CreateBuffer(raw)");

    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format                 = DXGI_FORMAT_R32_TYPELESS;
    srv.ViewDimension          = D3D11_SRV_DIMENSION_BUFFEREX;
    srv.BufferEx.NumElements   = byteSize / 4;
    srv.BufferEx.Flags         = D3D11_BUFFEREX_SRV_FLAG_RAW;
    ThrowIfFailed(m_device->CreateShaderResourceView(b.buffer.Get(), &srv, &b.srv), "CreateShaderResourceView(raw)");

    D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format             = DXGI_FORMAT_R32_TYPELESS;
    uav.ViewDimension      = D3D11_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = byteSize / 4;
    uav.Buffer.Flags       = D3D11_BUFFER_UAV_FLAG_RAW;
    ThrowIfFailed(m_device->CreateUnorderedAccessView(b.buffer.Get(), &uav, &b.uav), "CreateUnorderedAccessView(raw)");
    return b;
}

ComPtr<ID3D11Buffer> D3DContext::CreateConstantBuffer(UINT byteSize)
{
    ComPtr<ID3D11Buffer> cb;
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth      = (byteSize + 15) & ~15u;   // 16 バイト境界に切り上げ
    desc.Usage          = D3D11_USAGE_DYNAMIC;
    desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ThrowIfFailed(m_device->CreateBuffer(&desc, nullptr, &cb), "CreateBuffer(constant)");
    return cb;
}

ComPtr<ID3D11Buffer> D3DContext::CreateVertexBuffer(const void* data, UINT byteSize)
{
    ComPtr<ID3D11Buffer> vb;
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = byteSize;
    desc.Usage     = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init = { data, 0, 0 };
    ThrowIfFailed(m_device->CreateBuffer(&desc, &init, &vb), "CreateBuffer(vertex)");
    return vb;
}

void D3DContext::UpdateConstantBuffer(ID3D11Buffer* cb, const void* data, UINT byteSize)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    ThrowIfFailed(m_context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map(constant)");
    memcpy(mapped.pData, data, byteSize);
    m_context->Unmap(cb, 0);
}

ComPtr<ID3DBlob> D3DContext::CompileShader(const std::wstring& file, const char* entry, const char* target)
{
    std::wstring path = m_shaderDir + file;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    ComPtr<ID3DBlob> code, errors;
    HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    entry, target, flags, 0, &code, &errors);
    if (FAILED(hr))
    {
        std::string msg = "Shader compile error: " + std::string(entry) + "\n";
        if (errors) msg += (const char*)errors->GetBufferPointer();
        else        msg += "(file not found?) ";
        std::fprintf(stderr, "%s\n", msg.c_str());
        throw std::runtime_error(msg);
    }
    return code;
}

ComPtr<ID3D11ComputeShader> D3DContext::CreateCS(const std::wstring& file, const char* entry)
{
    ComPtr<ID3DBlob> blob = CompileShader(file, entry, "cs_5_0");
    ComPtr<ID3D11ComputeShader> cs;
    ThrowIfFailed(m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &cs), "CreateComputeShader");
    return cs;
}

ComPtr<ID3D11VertexShader> D3DContext::CreateVS(const std::wstring& file, const char* entry, ComPtr<ID3DBlob>* outBlob)
{
    ComPtr<ID3DBlob> blob = CompileShader(file, entry, "vs_5_0");
    ComPtr<ID3D11VertexShader> vs;
    ThrowIfFailed(m_device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs), "CreateVertexShader");
    if (outBlob) *outBlob = blob;
    return vs;
}

ComPtr<ID3D11PixelShader> D3DContext::CreatePS(const std::wstring& file, const char* entry)
{
    ComPtr<ID3DBlob> blob = CompileShader(file, entry, "ps_5_0");
    ComPtr<ID3D11PixelShader> ps;
    ThrowIfFailed(m_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ps), "CreatePixelShader");
    return ps;
}

void D3DContext::UnbindCompute()
{
    ID3D11ShaderResourceView*  nullSRV[8] = {};
    ID3D11UnorderedAccessView* nullUAV[8] = {};
    m_context->CSSetShaderResources(0, 8, nullSRV);
    m_context->CSSetUnorderedAccessViews(0, 8, nullUAV, nullptr);
}
