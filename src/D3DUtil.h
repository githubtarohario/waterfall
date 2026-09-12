#pragma once
// =====================================================================
//  D3DUtil.h
//  DirectX 11 の薄いユーティリティ層。
//  デバイス生成、バッファ / ビュー生成、シェーダーのコンパイルなど、
//  各モジュールで繰り返し使う定型処理をまとめる。
// =====================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

// HRESULT の失敗を例外に変換する
inline void ThrowIfFailed(HRESULT hr, const char* what)
{
    if (FAILED(hr))
    {
        char buf[512];
        sprintf_s(buf, "%s failed (hr=0x%08X)", what, (unsigned)hr);
        throw std::runtime_error(buf);
    }
}

// ---------------------------------------------------------------------
// GPU バッファ (StructuredBuffer / ByteAddressBuffer) と各ビューの組
// ---------------------------------------------------------------------
struct GpuBuffer
{
    ComPtr<ID3D11Buffer>              buffer;
    ComPtr<ID3D11ShaderResourceView>  srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    UINT count  = 0;   // 要素数
    UINT stride = 0;   // 要素サイズ [byte]
};

// ---------------------------------------------------------------------
// D3D11 デバイスとスワップチェーンの管理
// ---------------------------------------------------------------------
class D3DContext
{
public:
    void Initialize(HWND hwnd, int width, int height, bool debugLayer);

    ID3D11Device*        Device()  const { return m_device.Get(); }
    ID3D11DeviceContext* Context() const { return m_context.Get(); }
    IDXGISwapChain*      SwapChain() const { return m_swapChain.Get(); }
    ID3D11RenderTargetView* BackBufferRTV() const { return m_backBufferRTV.Get(); }
    ID3D11Texture2D*     BackBuffer() const { return m_backBuffer.Get(); }
    int Width()  const { return m_width; }
    int Height() const { return m_height; }

    // --- バッファ生成 ---
    // 構造化バッファ (SRV + UAV)。append=true で AppendStructuredBuffer 用のカウンタ付き UAV
    GpuBuffer CreateStructured(UINT stride, UINT count, const void* initData = nullptr, bool append = false);
    // 生バッファ (ByteAddressBuffer)。indirectArgs=true で DrawInstancedIndirect の引数用
    GpuBuffer CreateRaw(UINT byteSize, bool indirectArgs = false);
    // 定数バッファ (動的更新)
    ComPtr<ID3D11Buffer> CreateConstantBuffer(UINT byteSize);
    // 頂点バッファ (静的)
    ComPtr<ID3D11Buffer> CreateVertexBuffer(const void* data, UINT byteSize);
    // 定数バッファの更新
    void UpdateConstantBuffer(ID3D11Buffer* cb, const void* data, UINT byteSize);

    // --- シェーダー ---
    ComPtr<ID3DBlob> CompileShader(const std::wstring& file, const char* entry, const char* target);
    ComPtr<ID3D11ComputeShader> CreateCS(const std::wstring& file, const char* entry);
    ComPtr<ID3D11VertexShader>  CreateVS(const std::wstring& file, const char* entry, ComPtr<ID3DBlob>* outBlob = nullptr);
    ComPtr<ID3D11PixelShader>   CreatePS(const std::wstring& file, const char* entry);

    // --- コンピュートシェーダーの束縛解除 (SRV/UAV の競合防止) ---
    void UnbindCompute();

    // シェーダーファイルのディレクトリ (実行ファイルの隣の shaders/)
    const std::wstring& ShaderDir() const { return m_shaderDir; }

private:
    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_context;
    ComPtr<IDXGISwapChain>         m_swapChain;
    ComPtr<ID3D11Texture2D>        m_backBuffer;
    ComPtr<ID3D11RenderTargetView> m_backBufferRTV;
    std::wstring m_shaderDir;
    int m_width = 0, m_height = 0;
};
