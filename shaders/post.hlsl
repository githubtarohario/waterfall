// =====================================================================
//  post.hlsl
//  ポストプロセス: スーパーサンプリングの縮小 + トーンマップ + ガンマ + 周辺減光
// =====================================================================
#include "render_common.hlsli"

Texture2D<float4> gHdr : register(t0);   // 高解像度 HDR シーン

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

// 頂点バッファ無しの全画面三角形
VSOut VS_Fullscreen(uint vid : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((vid << 1) & 2, vid & 2);
    o.uv  = uv;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float4 PS_Post(VSOut i) : SV_TARGET
{
    // バイリニアで 2x2 テクセルの平均 (SSAA の縮小)
    float3 c = gHdr.SampleLevel(gLinearClamp, i.uv, 0).rgb;

    // 緩やかなトーンマップ (ハイライトの白飛びを抑える)
    c = c / (1.0 + c * 0.35) * 1.35;

    // 周辺減光 (動画の四隅の暗さを再現)
    float2 d = (i.uv - 0.5) * float2(1.0, 0.75);
    float  vig = 1.0 - 0.55 * pow(saturate(length(d) * 1.6), 2.0);
    c *= vig;

    // ガンマ補正
    c = pow(saturate(c), 1.0 / 2.2);
    return float4(c, 1.0);
}
