// =====================================================================
//  render_common.hlsli
//  描画シェーダー共通の定数バッファと環境光関数。
//  C++ 側の FrameParams 構造体 (Renderer.h) と完全に一致させること。
// =====================================================================
#ifndef RENDER_COMMON_HLSLI
#define RENDER_COMMON_HLSLI

cbuffer FrameParams : register(b0)
{
    float4x4 viewProj;        // ビュー射影行列
    float4x4 view;            // ビュー行列 (法線のスクリーン空間変換に使用)
    float4x4 lightViewProj;   // シャドウマップ用の光源ビュー射影行列
    float4   cameraPos;       // xyz = カメラ位置
    float4   lightDir;        // xyz = 光源へ向かう単位ベクトル
    float4   screenSize;      // x,y = 描画解像度, z,w = その逆数
    float4   params;          // x = 時刻, y = near, z = far, w = 未使用
    float4   tank;            // x = 内側半幅, y = 外側半幅, z = 壁高さ, w = 床の高さ
};

SamplerState           gLinearClamp : register(s0);
SamplerComparisonState gShadowCmp   : register(s1);

// ---------------------------------------------------------------------
// スタジオ環境 (映り込み用)。動画と同じ無彩色のグラデーション +
// 大きなソフトボックス光源のハイライト。
// ---------------------------------------------------------------------
float3 EnvColor(float3 d, float softScale)
{
    float  up   = saturate(d.y * 0.5 + 0.5);
    float3 low  = float3(0.14, 0.14, 0.15);      // 床側 (暗いグレー)
    float3 high = float3(0.42, 0.42, 0.43);      // 天井側 (明るいグレー)
    float3 sky  = lerp(low, high, up);
    // ソフトボックス: 光源方向の周りに広く柔らかい白いハイライト
    // 反射率 2〜4% を掛けても白く飛ぶよう、放射輝度は大きめにしておく
    float  soft = pow(saturate(dot(d, lightDir.xyz)), 22.0) * 18.0 * softScale;
    return sky + soft;
}

// シャドウマップ参照 (4x4 PCF で柔らかい影)
float ShadowFactor(Texture2D<float> shadowMap, float3 worldPos, float bias)
{
    float4 lp = mul(float4(worldPos, 1.0), lightViewProj);
    float2 uv = lp.xy / lp.w * float2(0.5, -0.5) + 0.5;
    float  z  = lp.z / lp.w - bias;
    if (any(uv < 0.0) || any(uv > 1.0)) return 1.0;

    float2 texel = 2.5 / 2048.0;   // 2.5 テクセル間隔で広めにサンプリングし柔らかい半影にする
    float  sum = 0.0;
    [unroll]
    for (int y = -2; y <= 1; ++y)
    [unroll]
    for (int x = -2; x <= 1; ++x)
        sum += shadowMap.SampleCmpLevelZero(gShadowCmp, uv + float2(x + 0.5, y + 0.5) * texel, z);
    return sum / 16.0;
}

// 非線形深度 → ビュー空間の距離
float LinearizeDepth(float d)
{
    float n = params.y, f = params.z;
    return n * f / (f - d * (f - n));
}

#endif // RENDER_COMMON_HLSLI
