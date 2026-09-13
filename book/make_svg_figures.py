# -*- coding: utf-8 -*-
# =====================================================================
#  make_svg_figures.py
#  解説書用の模式図 (SVG) を生成する。book/figures/ に出力。
# =====================================================================
import io, os, random

F = os.path.join(os.path.dirname(__file__), 'figures')
os.makedirs(F, exist_ok=True)

def write(name, lines):
    io.open(os.path.join(F, name), 'w', encoding='utf-8').write('\n'.join(lines))

# ---------------------------------------------------------------------
# 1. マーチングキューブの頂点・エッジ番号 (斜投影)
# ---------------------------------------------------------------------
def proj(x, y, z, s=150, ox=70, oy=200):
    return (ox + x * s + z * s * 0.45, oy - y * s - z * s * 0.35)

V = [(v & 1, (v >> 1) & 1, (v >> 2) & 1) for v in range(8)]
E = [(0,1),(2,3),(4,5),(6,7),(0,2),(1,3),(4,6),(5,7),(0,4),(1,5),(2,6),(3,7)]
svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 330 250" width="330" height="250" font-family="Segoe UI, Meiryo, sans-serif">']
for i, (a, b) in enumerate(E):
    (x1, y1), (x2, y2) = proj(*V[a]), proj(*V[b])
    col = {0: '#c0392b', 1: '#27ae60', 2: '#2980b9'}[i // 4]
    svg.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{col}" stroke-width="2"/>')
    mx, my = (x1 + x2) / 2, (y1 + y2) / 2
    svg.append(f'<rect x="{mx-9}" y="{my-8}" width="18" height="15" rx="3" fill="white" stroke="{col}"/>')
    svg.append(f'<text x="{mx}" y="{my+4}" font-size="10" text-anchor="middle" fill="{col}">e{i}</text>')
for v, (x, y, z) in enumerate(V):
    px, py = proj(x, y, z)
    svg.append(f'<circle cx="{px}" cy="{py}" r="9" fill="#1a222b"/>')
    svg.append(f'<text x="{px}" y="{py+4}" font-size="10" text-anchor="middle" fill="white" font-weight="bold">{v}</text>')
    svg.append(f'<text x="{px+12}" y="{py-10}" font-size="9" fill="#555">({x},{y},{z})</text>')
svg.append('<text x="8" y="242" font-size="10" fill="#c0392b">x 方向: e0-e3</text>'
           '<text x="110" y="242" font-size="10" fill="#27ae60">y 方向: e4-e7</text>'
           '<text x="212" y="242" font-size="10" fill="#2980b9">z 方向: e8-e11</text>')
svg.append('</svg>')
write('mc_numbering.svg', svg)

# ---------------------------------------------------------------------
# 2. 一様格子と近傍セル (2D で 9 近傍)
# ---------------------------------------------------------------------
svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 420 240" width="420" height="240" font-family="Segoe UI, Meiryo, sans-serif">']
cs = 40; ox, oy = 20, 20
random.seed(3)
for i in range(5):
    for j in range(5):
        fill = '#d9eef0' if (1 <= i <= 3 and 1 <= j <= 3) else '#f7f9fb'
        if (i, j) == (2, 2): fill = '#a9d8dc'
        svg.append(f'<rect x="{ox+i*cs}" y="{oy+j*cs}" width="{cs}" height="{cs}" fill="{fill}" stroke="#7b8794"/>')
for k in range(60):
    x = ox + random.random() * 5 * cs; y = oy + random.random() * 5 * cs
    svg.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="2.6" fill="#4c5866"/>')
cx, cy = ox + 2.5 * cs, oy + 2.5 * cs
svg.append(f'<circle cx="{cx}" cy="{cy}" r="5" fill="#c0392b"/>')
svg.append(f'<circle cx="{cx}" cy="{cy}" r="{cs}" fill="none" stroke="#c0392b" stroke-width="1.5" stroke-dasharray="4 3"/>')
svg.append(f'<text x="{cx+8}" y="{cy-cs+2}" font-size="10" fill="#c0392b">半径 h</text>')
texts = ['セル幅 = h', '', '粒子 i (赤) の近傍候補は', '自身のセルと隣接セル', '(2D: 9 個, 3D: 27 個) だけ。', '',
         '同じセルの粒子は並べ替えで', 'メモリ上でも連続に並ぶので', 'cellStart[c] .. cellStart[c+1]', 'の範囲を走査すればよい。']
for k, t in enumerate(texts):
    w = 'bold' if k == 0 else 'normal'
    svg.append(f'<text x="240" y="{40+k*16}" font-size="11" fill="#1a222b" font-weight="{w}">{t}</text>')
svg.append('</svg>')
write('grid_neighbors.svg', svg)

# ---------------------------------------------------------------------
# 3. カウンティングソートの流れ
# ---------------------------------------------------------------------
svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 560 250" width="560" height="250" font-family="Consolas, Meiryo, monospace" font-size="11">']
def row(y, title, vals, colors=None):
    svg.append(f'<text x="8" y="{y+15}" font-size="11" font-family="Meiryo, sans-serif" fill="#1a222b">{title}</text>')
    for i, v in enumerate(vals):
        c = colors[i] if colors else '#eef2f5'
        svg.append(f'<rect x="{170+i*34}" y="{y}" width="32" height="22" fill="{c}" stroke="#7b8794"/>')
        svg.append(f'<text x="{186+i*34}" y="{y+15}" text-anchor="middle" fill="#1a222b">{v}</text>')
cellcol = {0: '#fde2e1', 1: '#e3f4e5', 2: '#dfeaf7', 3: '#f5e9d6'}
parts = [('p0', 2), ('p1', 0), ('p2', 1), ('p3', 2), ('p4', 0), ('p5', 2), ('p6', 3), ('p7', 1)]
for i, (n, _) in enumerate(parts):
    svg.append(f'<text x="{186+i*34}" y="12" font-size="9" fill="#666" text-anchor="middle">{n}</text>')
row(18, '所属セル (CS_Count)', [str(c) for _, c in parts], [cellcol[c] for _, c in parts])
row(64, 'cellCount', ['2', '2', '3', '1'], [cellcol[i] for i in range(4)])
row(104, 'cellStart (排他的累積和)', ['0', '2', '4', '7', '8'], [cellcol[i] for i in range(4)] + ['#eee'])
row(152, '並べ替え後 (CS_Reorder)', ['p1', 'p4', 'p2', 'p7', 'p0', 'p3', 'p5', 'p6'], [cellcol[c] for c in [0, 0, 1, 1, 2, 2, 2, 3]])
for i, (lab, x) in enumerate([('セル0', 186 + 0.5 * 34), ('セル1', 186 + 2.5 * 34), ('セル2', 186 + 5 * 34), ('セル3', 186 + 7 * 34)]):
    svg.append(f'<text x="{x}" y="192" font-size="9" fill="#666" text-anchor="middle">{lab}</text>')
svg.append('<text x="8" y="228" font-size="11" font-family="Meiryo, sans-serif" fill="#1a222b">書き込み先 = cellStart[cell] + cellOffset  (cellOffset は Count 時のアトミック加算で得た順序番号)</text>')
svg.append('</svg>')
write('counting_sort.svg', svg)

# ---------------------------------------------------------------------
# 4. 厚みの推定
# ---------------------------------------------------------------------
svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 520 220" width="520" height="220" font-family="Meiryo, sans-serif" font-size="11">']
svg.append('<rect x="0" y="0" width="520" height="220" fill="white"/>')
svg.append('<path d="M60 30 L60 190" stroke="#7b8794"/><text x="16" y="115" fill="#1a222b">カメラ</text><polygon points="60,100 75,92 75,108" fill="#1a222b"/>')
svg.append('<path d="M220 40 Q 300 20 370 60 L 370 150 Q 300 190 220 150 Z" fill="#a9d8dc" stroke="#0f7f8a" stroke-width="2"/>')
svg.append('<text x="290" y="105" fill="#0a5f68" text-anchor="middle">水</text>')
svg.append('<rect x="470" y="30" width="12" height="160" fill="#7b8794"/><text x="450" y="205" fill="#1a222b">背景 (床)</text>')
svg.append('<line x1="75" y1="100" x2="470" y2="100" stroke="#c0392b" stroke-dasharray="5 4"/>')
svg.append('<circle cx="222" cy="100" r="4" fill="#c0392b"/><text x="196" y="88" fill="#c0392b">表面 z_front</text>')
svg.append('<circle cx="368" cy="100" r="4" fill="#c0392b"/><text x="340" y="126" fill="#c0392b">裏面 z_back</text>')
svg.append('<circle cx="470" cy="100" r="4" fill="#c0392b"/><text x="404" y="88" fill="#c0392b">背景 z_scene</text>')
svg.append('<line x1="222" y1="160" x2="368" y2="160" stroke="#1a222b"/><line x1="222" y1="154" x2="222" y2="166" stroke="#1a222b"/><line x1="368" y1="154" x2="368" y2="166" stroke="#1a222b"/>')
svg.append('<text x="295" y="180" text-anchor="middle" fill="#1a222b">厚み = min(z_scene, z_back) − z_front</text>')
svg.append('</svg>')
write('thickness.svg', svg)

# ---------------------------------------------------------------------
# 5. 描画パス
# ---------------------------------------------------------------------
svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 150" width="640" height="150" font-family="Meiryo, sans-serif" font-size="10.5">']
boxes = [('シャドウ', 'マップ', '#eef2f5'), ('床', '(HDR RT)', '#eef2f5'), ('色/深度', 'コピー', '#f5e9d6'), ('水の', '裏面深度', '#f5e9d6'),
         ('水面', '(HDR RT)', '#d9eef0'), ('ガラス', '(HDR RT)', '#d9eef0'), ('ポスト', '→ 画面', '#e3f4e5')]
for i, (t1, t2, c) in enumerate(boxes):
    x = 10 + i * 90
    svg.append(f'<rect x="{x}" y="40" width="78" height="56" rx="6" fill="{c}" stroke="#7b8794"/>')
    svg.append(f'<text x="{x+39}" y="62" text-anchor="middle" fill="#1a222b">{t1}</text>')
    svg.append(f'<text x="{x+39}" y="77" text-anchor="middle" fill="#1a222b">{t2}</text>')
    if i < 6:
        svg.append(f'<line x1="{x+78}" y1="68" x2="{x+90}" y2="68" stroke="#1a222b" stroke-width="1.5"/><polygon points="{x+90},68 {x+85},64 {x+85},72" fill="#1a222b"/>')
svg.append('<text x="10" y="20" font-size="11" fill="#4c5866">Renderer::Render() の 7 段階。中央の 2 段は「水を描く前の画」を屈折と厚み推定のために保存する。</text>')
svg.append('<text x="10" y="128" font-size="10" fill="#4c5866">HDR RT = R16G16B16A16_FLOAT。内部解像度はウィンドウの 2 倍 (SSAA)。最終段で 2×2 平均・トーンマップ・ガンマ補正。</text>')
svg.append('</svg>')
write('render_passes.svg', svg)

# ---------------------------------------------------------------------
# 6. 全体パイプライン
# ---------------------------------------------------------------------
svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 170" width="640" height="170" font-family="Meiryo, sans-serif" font-size="11">']
stages = [('SPH 法', ['粒子の位置・速度', '(GPU, 45 サブステップ)'], '#d9eef0'),
          ('スカラー場', ['粒子 → 格子点の値', '(GPU)'], '#f5e9d6'),
          ('マーチングキューブ', ['等値面 → 三角形', '(GPU, Append)'], '#f5e9d6'),
          ('レンダリング', ['影・反射・屈折', '(GPU)'], '#e3f4e5')]
for i, (t, d, c) in enumerate(stages):
    x = 10 + i * 158
    svg.append(f'<rect x="{x}" y="30" width="140" height="80" rx="8" fill="{c}" stroke="#7b8794"/>')
    svg.append(f'<text x="{x+70}" y="55" text-anchor="middle" font-weight="bold" fill="#1a222b">{t}</text>')
    for k, line in enumerate(d):
        svg.append(f'<text x="{x+70}" y="{76+k*15}" text-anchor="middle" fill="#4c5866" font-size="10">{line}</text>')
    if i < 3:
        svg.append(f'<line x1="{x+140}" y1="70" x2="{x+158}" y2="70" stroke="#1a222b" stroke-width="1.5"/><polygon points="{x+158},70 {x+152},65 {x+152},75" fill="#1a222b"/>')
svg.append('<text x="10" y="140" fill="#4c5866" font-size="10.5">1 描画フレーム (= 動画の 1/24 秒) ごとにこの 4 段階を通る。粒子データは GPU から出ないので CPU との往復は無い。</text>')
svg.append('</svg>')
write('pipeline.svg', svg)

# ---------------------------------------------------------------------
# 7. カメラ座標系と行列の流れ
# ---------------------------------------------------------------------
svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 120" width="640" height="120" font-family="Meiryo, sans-serif" font-size="11">']
stages = [('ワールド座標', 'メッシュの頂点 (m)'), ('ビュー座標', 'カメラが原点 (view)'), ('クリップ座標', '射影 (proj), w で割る'), ('スクリーン座標', 'ピクセル (viewport)')]
for i, (t, d) in enumerate(stages):
    x = 10 + i * 158
    svg.append(f'<rect x="{x}" y="25" width="140" height="60" rx="8" fill="#eef2f5" stroke="#7b8794"/>')
    svg.append(f'<text x="{x+70}" y="48" text-anchor="middle" font-weight="bold" fill="#1a222b">{t}</text>')
    svg.append(f'<text x="{x+70}" y="68" text-anchor="middle" fill="#4c5866" font-size="10">{d}</text>')
    if i < 3:
        svg.append(f'<line x1="{x+140}" y1="55" x2="{x+158}" y2="55" stroke="#1a222b" stroke-width="1.5"/><polygon points="{x+158},55 {x+152},50 {x+152},60" fill="#1a222b"/>')
svg.append('<text x="10" y="108" fill="#4c5866" font-size="10.5">頂点シェーダーは mul(float4(p,1), viewProj) でクリップ座標まで変換し、ラスタライザが残りを行う。</text>')
svg.append('</svg>')
write('coords.svg', svg)

print('svg figures:', sorted(f for f in os.listdir(F) if f.endswith('.svg')))
