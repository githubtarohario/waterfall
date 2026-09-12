// =====================================================================
//  scene.hlsl
//  スタジオの床とガラス水槽の描画。
//    VS_Mesh  : 位置 + 法線の通常メッシュ
//    PS_Floor : 無彩色の床。柔らかい照明の落ち込み + 水の影 + 遠方フォグ
//    PS_Glass : 薄いガラス (フレネル反射 + 半透明)
// =====================================================================
#include "render_common.hlsli"

Texture2D<float> gShadowMap : register(t3);

struct VSIn  { float3 pos : POSITION; float3 normal : NORMAL; };
struct VSOut
{
    float4 pos      : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
};

VSOut VS_Mesh(VSIn i)
{
    VSOut o;
    o.pos      = mul(float4(i.pos, 1.0), viewProj);
    o.worldPos = i.pos;
    o.normal   = i.normal;
    return o;
}

// ---------------------------------------------------------------------
// 床: 動画のようなグレーのサイクロラマ (画面左上が明るく右下へ暗くなる)
// ---------------------------------------------------------------------
float4 PS_Floor(VSOut i) : SV_TARGET
{
    float3 p = i.worldPos;

    // 大きな光源による柔らかい明るさの分布 (左奥が明るい)
    float2 lightCenter = float2(1.6, 0.1);
    float  d2 = dot(p.xz - lightCenter, p.xz - lightCenter);
    float  pool = 0.36 + 0.50 * exp(-d2 / (2.0 * 2.0 * 2.0));

    // 水槽のガラス底板越しは僅かに暗い
    float2 a = abs(p.xz);
    float  inTank = (1.0 - smoothstep(tank.y - 0.01, tank.y + 0.01, max(a.x, a.y)));
    pool *= lerp(1.0, 0.86, inTank);

    // 水が落とす影
    float shadow = ShadowFactor(gShadowMap, p, 0.002);
    pool *= lerp(0.66, 1.0, shadow);

    float3 color = float3(0.46, 0.46, 0.47) * pool;

    // 遠方は背景色へ溶け込ませる
    float dist = distance(cameraPos.xyz, p);
    float fog  = 1.0 - exp(-max(0.0, dist - 2.0) * 0.20);
    color = lerp(color, float3(0.27, 0.27, 0.28), fog);
    return float4(color, 1.0);
}

// ---------------------------------------------------------------------
// ガラス水槽: フレネルによる映り込みと僅かな灰色の着色
// ---------------------------------------------------------------------
float4 PS_Glass(VSOut i, bool isFront : SV_IsFrontFace) : SV_TARGET
{
    float3 n = normalize(i.normal);
    if (!isFront) n = -n;                          // 裏面は法線を反転
    float3 v = normalize(cameraPos.xyz - i.worldPos);
    float  ndv = saturate(dot(n, v));
    float  fresnel = 0.04 + 0.96 * pow(1.0 - ndv, 5.0);

    float3 refl = EnvColor(reflect(-v, n), 0.25);   // ガラスは面が重なるので控えめに
    float3 hv   = normalize(lightDir.xyz + v);
    float  spec = pow(saturate(dot(n, hv)), 200.0) * 1.2;

    // 乗算済みアルファ (SrcBlend=ONE, DestBlend=INV_SRC_ALPHA):
    //   rgb = 加算する光 (反射 + ハイライト + 僅かな着色), a = 背景を遮る割合
    float3 color = refl * fresnel * 0.5 + spec * 0.3 + float3(0.55, 0.56, 0.56) * 0.01;
    float  alpha = saturate(fresnel * 0.5 + 0.015);
    return float4(color, alpha);
}
