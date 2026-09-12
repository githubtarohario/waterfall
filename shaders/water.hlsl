// =====================================================================
//  water.hlsl
//  マーチングキューブ法で生成した水面メッシュの描画。
//    VS_Water        : 三角形バッファから頂点を取り出す (入力レイアウト不要)
//    VS_WaterShadow  : シャドウマップ用 (光源視点)
//    PS_Water        : フレネル反射 + 屈折 + 厚みによる吸収 + ハイライト
//    VS/PS_Particles : デバッグ用の粒子ポイント表示
// =====================================================================
#include "render_common.hlsli"

struct MCVertex   { float3 p; float3 n; };
struct MCTriangle { MCVertex v0, v1, v2; };
StructuredBuffer<MCTriangle> gTriangles : register(t0);   // MC の出力

Texture2D<float4> gSceneColor : register(t1);   // 水を描く前のシーン (屈折の参照先)
Texture2D<float>  gSceneDepth : register(t2);   // 同・深度 (厚み推定に使用)
Texture2D<float>  gShadowMap  : register(t3);
Texture2D<float>  gWaterBack  : register(t4);   // 水面メッシュの裏面深度 (厚み推定に使用)

struct VSOut
{
    float4 pos      : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float  viewZ    : TEXCOORD2;   // ビュー空間の距離
};

// 頂点 ID から三角形バッファの頂点を取り出す
MCVertex FetchVertex(uint vid)
{
    MCTriangle t = gTriangles[vid / 3];
    uint c = vid % 3;
    if (c == 0) return t.v0;
    if (c == 1) return t.v1;
    return t.v2;
}

VSOut VS_Water(uint vid : SV_VertexID)
{
    MCVertex v = FetchVertex(vid);
    VSOut o;
    o.pos      = mul(float4(v.p, 1.0), viewProj);
    o.worldPos = v.p;
    o.normal   = v.n;
    o.viewZ    = mul(float4(v.p, 1.0), view).z;
    return o;
}

float4 VS_WaterShadow(uint vid : SV_VertexID) : SV_POSITION
{
    MCVertex v = FetchVertex(vid);
    return mul(float4(v.p, 1.0), lightViewProj);
}

// 裏面深度パス用 (カメラ視点、位置のみ)
float4 VS_WaterDepth(uint vid : SV_VertexID) : SV_POSITION
{
    MCVertex v = FetchVertex(vid);
    return mul(float4(v.p, 1.0), viewProj);
}

float4 PS_Water(VSOut i) : SV_TARGET
{
    float3 n = normalize(i.normal);
    float3 v = normalize(cameraPos.xyz - i.worldPos);
    float  ndv = saturate(dot(n, v));

    // --- フレネル (Schlick)。水の反射率 ≒ 2% ---
    float fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);

    // --- 反射: スタジオ環境 ---
    float3 refl = EnvColor(reflect(-v, n), 1.0);

    // --- 屈折: 背景をスクリーン空間で法線方向にずらして参照 ---
    float2 uv    = i.pos.xy * screenSize.zw;
    float3 nView = mul(n, (float3x3)view);                  // ビュー空間の法線
    float2 uvR   = uv + nView.xy * float2(1.0, -1.0) * 0.035;
    // ずらした先が水より手前の物体なら元の位置を使う
    float sceneZ = LinearizeDepth(gSceneDepth.SampleLevel(gLinearClamp, uvR, 0));
    if (sceneZ < i.viewZ) { uvR = uv; sceneZ = LinearizeDepth(gSceneDepth.SampleLevel(gLinearClamp, uv, 0)); }
    float3 refr = gSceneColor.SampleLevel(gLinearClamp, uvR, 0).rgb;

    // --- 厚みによる吸収。厚み = 表面から「裏面 or 背後の物体」の近い方まで ---
    float  backZ     = LinearizeDepth(gWaterBack.SampleLevel(gLinearClamp, uv, 0));
    float  thickness = max(0.0, min(sceneZ, backZ) - i.viewZ);
    float3 absorb    = float3(1.6, 1.4, 1.5);               // 動画の水は僅かに緑がかった灰色のガラス質
    refr *= exp(-thickness * absorb);
    // 内部散乱による僅かな明るさ (泡立ちの白っぽさ)
    refr += float3(0.03, 0.034, 0.034) * (1.0 - exp(-thickness * 3.0));

    // --- ハイライト (Blinn-Phong) ---
    float3 hv   = normalize(lightDir.xyz + v);
    float  spec = pow(saturate(dot(n, hv)), 220.0) * 2.0;
    float  shadow = ShadowFactor(gShadowMap, i.worldPos, 0.0015);

    float3 color = lerp(refr, refl, fresnel) + spec * shadow;
    return float4(color, 1.0);
}

// ---------------------------------------------------------------------
// デバッグ用: 粒子をスクリーン空間の小さな四角形で表示
// ---------------------------------------------------------------------
StructuredBuffer<float4> gParticlePos : register(t4);

struct PVSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

PVSOut VS_Particles(uint vid : SV_VertexID)
{
    uint  pid = vid / 6;
    uint  corner = vid % 6;
    // 2 三角形 = 6 頂点で四角形を作る
    float2 offs[6] = { float2(-1,-1), float2(1,-1), float2(-1,1), float2(-1,1), float2(1,-1), float2(1,1) };
    float4 clip = mul(float4(gParticlePos[pid].xyz, 1.0), viewProj);
    float2 size = float2(2.0, 2.0) * screenSize.zw * 2.0;   // 数ピクセル
    clip.xy += offs[corner] * size * clip.w;
    PVSOut o; o.pos = clip; o.uv = offs[corner];
    return o;
}

float4 PS_Particles(PVSOut i) : SV_TARGET
{
    if (dot(i.uv, i.uv) > 1.0) discard;
    return float4(0.3, 0.6, 1.0, 1.0);
}
