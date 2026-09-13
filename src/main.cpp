// =====================================================================
//  main.cpp
//  【SPH法】水しぶきシミュレーション (DirectX 11)
//
//  動画と同じ「2 つの水塊がガラス水槽へ落下し、盛大な飛沫を上げて収まる」
//  シーンを、GPU WCSPH + マーチングキューブ法で再現する。
//
//  コマンドライン:
//    --spacing <m>   粒子間隔 [m] (既定 0.014。小さいほど高精細・低速)
//    --record [dir]  各フレームを BMP で保存 (既定 capture/)。24fps 固定ステップ
//    --frames <n>    n フレーム描画したら自動終了
//    --particles     粒子を点で表示するデバッグモードで開始
//    --nowater       水面メッシュを描かない (粒子表示と組み合わせて使う)
//    --debug         D3D11 デバッグレイヤーを有効化
//    --resttest      水塊 A を床に静置する安定性テストシーン
//
//  操作:
//    Space  : 一時停止 / 再開       S : 1 フレーム進める
//    R      : リセット               P : 粒子表示の切り替え
//    W      : 水面メッシュ表示の切り替え  G : ガラス水槽表示の切り替え
//    右ドラッグ : カメラ回転        ホイール : ズーム
//    Esc    : 終了
// =====================================================================
#include "D3DUtil.h"
#include "SimConfig.h"
#include "SPHSimulator.h"
#include "MarchingCubes.h"
#include "Renderer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <direct.h>

namespace
{
    // アプリケーション状態 (ウィンドウプロシージャから参照)
    struct AppState
    {
        bool paused    = false;
        bool stepOnce  = false;
        bool resetReq  = false;
        bool quit      = false;
        bool toggleParticles = false;
        bool toggleWater     = false;
        bool toggleGlass     = false;
        bool rightDrag = false;
        POINT lastMouse = {};
        float orbitYaw = 0, orbitPitch = 0, zoom = 0;   // 入力の累積
    } g_app;

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_DESTROY: PostQuitMessage(0); return 0;
        case WM_KEYDOWN:
            switch (wp)
            {
            case VK_ESCAPE: g_app.quit = true; break;
            case VK_SPACE:  g_app.paused = !g_app.paused; break;
            case 'S':       g_app.stepOnce = true; break;
            case 'R':       g_app.resetReq = true; break;
            case 'P':       g_app.toggleParticles = true; break;
            case 'W':       g_app.toggleWater = true; break;
            case 'G':       g_app.toggleGlass = true; break;
            }
            return 0;
        case WM_RBUTTONDOWN: g_app.rightDrag = true; SetCapture(hwnd); GetCursorPos(&g_app.lastMouse); return 0;
        case WM_RBUTTONUP:   g_app.rightDrag = false; ReleaseCapture(); return 0;
        case WM_MOUSEMOVE:
            if (g_app.rightDrag)
            {
                POINT p; GetCursorPos(&p);
                g_app.orbitYaw   += (p.x - g_app.lastMouse.x) * 0.3f;
                g_app.orbitPitch += (p.y - g_app.lastMouse.y) * 0.3f;
                g_app.lastMouse = p;
            }
            return 0;
        case WM_MOUSEWHEEL: g_app.zoom += GET_WHEEL_DELTA_WPARAM(wp) / 120.0f; return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // バックバッファを 24bit BMP として保存する
    void SaveBackBufferBMP(D3DContext& d3d, const std::string& path)
    {
        ID3D11Device* dev = d3d.Device();
        ID3D11DeviceContext* ctx = d3d.Context();

        D3D11_TEXTURE2D_DESC desc;
        d3d.BackBuffer()->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(dev->CreateTexture2D(&desc, nullptr, &staging))) return;
        ctx->CopyResource(staging.Get(), d3d.BackBuffer());

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;

        const int w = desc.Width, h = desc.Height;
        const int rowBytes = (w * 3 + 3) & ~3;          // BMP の行は 4 バイト境界
        std::vector<unsigned char> pixels(rowBytes * h);
        for (int y = 0; y < h; ++y)
        {
            const unsigned char* src = (const unsigned char*)mapped.pData + (h - 1 - y) * mapped.RowPitch;  // BMP は下から上
            unsigned char* dst = pixels.data() + y * rowBytes;
            for (int x = 0; x < w; ++x)
            {
                dst[x * 3 + 0] = src[x * 4 + 2];   // B
                dst[x * 3 + 1] = src[x * 4 + 1];   // G
                dst[x * 3 + 2] = src[x * 4 + 0];   // R
            }
        }
        ctx->Unmap(staging.Get(), 0);

#pragma pack(push, 1)
        struct { uint16_t type; uint32_t size; uint16_t r1, r2; uint32_t offset; } fileHdr =
            { 0x4D42, uint32_t(54 + pixels.size()), 0, 0, 54 };
        struct { uint32_t size; int32_t w, h; uint16_t planes, bpp; uint32_t comp, imgSize; int32_t xppm, yppm; uint32_t clrUsed, clrImp; } infoHdr =
            { 40, w, h, 1, 24, 0, uint32_t(pixels.size()), 2835, 2835, 0, 0 };
#pragma pack(pop)
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "wb") == 0 && f)
        {
            fwrite(&fileHdr, sizeof(fileHdr), 1, f);
            fwrite(&infoHdr, sizeof(infoHdr), 1, f);
            fwrite(pixels.data(), 1, pixels.size(), f);
            fclose(f);
        }
    }
}

int main(int argc, char** argv)
{
    // ------------------------------------------------------------
    // コマンドライン解析
    // ------------------------------------------------------------
    float spacing = cfg::DEFAULT_SPACING;
    bool  record = false, debugLayer = false, particles = false, restTest = false, noWater = false;
    int   maxFrames = -1;
    std::string recordDir = "capture";
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--spacing") && i + 1 < argc) spacing = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--record"))
        {
            record = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') recordDir = argv[++i];
        }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--particles")) particles = true;
        else if (!strcmp(argv[i], "--nowater")) noWater = true;
        else if (!strcmp(argv[i], "--debug")) debugLayer = true;
        else if (!strcmp(argv[i], "--resttest")) restTest = true;   // 水塊を床に静置する安定性テスト
    }
    if (record) _mkdir(recordDir.c_str());

    // ------------------------------------------------------------
    // ウィンドウ
    // ------------------------------------------------------------
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"SPHSplashWindow";
    RegisterClassW(&wc);

    RECT rc = { 0, 0, cfg::WINDOW_W, cfg::WINDOW_H };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"【SPH法】水しぶきシミュレーション / Water Splash Simulation (DirectX 11)",
                              WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                              nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    // ------------------------------------------------------------
    // 初期化
    // ------------------------------------------------------------
    D3DContext    d3d;
    SPHSimulator  sim;
    MarchingCubes mc;
    Renderer      renderer;
    try
    {
        d3d.Initialize(hwnd, cfg::WINDOW_W, cfg::WINDOW_H, debugLayer);
        sim.Initialize(d3d, spacing, restTest);
        mc.Initialize(d3d, spacing);
        renderer.Initialize(d3d);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "Initialization failed: %s\n", e.what());
        MessageBoxA(hwnd, e.what(), "Initialization failed", MB_ICONERROR);
        return 1;
    }
    renderer.showParticles = particles;
    renderer.showWater     = !noWater;

    // ------------------------------------------------------------
    // メインループ: 1 描画フレーム = 動画の 1 フレーム (1/24 秒)
    // ------------------------------------------------------------
    int  frameIndex = 0;
    bool firstFrame = true;
    auto lastStat = std::chrono::steady_clock::now();
    MSG  msg = {};
    while (!g_app.quit)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) g_app.quit = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g_app.quit) break;

        // --- 入力の反映 ---
        if (g_app.resetReq) { sim.Reset(); frameIndex = 0; firstFrame = true; g_app.resetReq = false; }
        if (g_app.toggleParticles) { renderer.showParticles = !renderer.showParticles; g_app.toggleParticles = false; }
        if (g_app.toggleWater)     { renderer.showWater = !renderer.showWater; g_app.toggleWater = false; }
        if (g_app.toggleGlass)     { renderer.showGlass = !renderer.showGlass; g_app.toggleGlass = false; }
        OrbitCamera& cam = renderer.Camera();
        cam.yawDeg   -= g_app.orbitYaw;
        cam.pitchDeg  = std::max(5.0f, std::min(85.0f, cam.pitchDeg + g_app.orbitPitch));
        cam.distance  = std::max(0.5f, cam.distance * std::pow(0.9f, g_app.zoom));
        g_app.orbitYaw = g_app.orbitPitch = g_app.zoom = 0;

        auto t0 = std::chrono::steady_clock::now();

        // --- シミュレーション (最初のフレームは初期状態を表示) ---
        bool advance = (!g_app.paused || g_app.stepOnce) && !firstFrame;
        if (advance) { sim.StepFrame(); ++frameIndex; g_app.stepOnce = false; }
        firstFrame = false;

        // --- 水面メッシュ生成と描画 ---
        sim.SortParticles();          // MC の近傍探索用に格子を最新化
        mc.Build(sim);
        renderer.Render(sim, mc, sim.SimTime());
        d3d.SwapChain()->Present(record ? 0 : 1, 0);

        // --- 録画 ---
        if (record && (advance || frameIndex == 0))
        {
            char name[512];
            std::snprintf(name, sizeof(name), "%s/frame_%04d.bmp", recordDir.c_str(), frameIndex);
            SaveBackBufferBMP(d3d, name);
        }

        // --- 統計表示 (タイトルバー) ---
        auto t1 = std::chrono::steady_clock::now();
        float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
        if (std::chrono::duration<float>(t1 - lastStat).count() > 0.25f)
        {
            uint32_t tris = mc.LastTriangleCount();
            wchar_t title[256];
            swprintf_s(title, L"SPH 水しぶき | t=%.2fs frame=%d | %u particles | %u tris | %.1f ms/frame (%u substeps)%s",
                       sim.SimTime(), frameIndex, sim.NumParticles(), tris, ms, sim.SubstepsPerFrame(),
                       g_app.paused ? L" [PAUSED]" : L"");
            SetWindowTextW(hwnd, title);
            lastStat = t1;
        }

        if (maxFrames >= 0 && frameIndex >= maxFrames) break;
    }
    return 0;
}
