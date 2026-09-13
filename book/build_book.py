# -*- coding: utf-8 -*-
# =====================================================================
#  build_book.py
#  解説書を組み立てて HTML と PDF を出力する。
#
#    python book/build_book.py          → book/CG解説書.html, book/CG解説書.pdf
#
#  章は book/chapters/*.html (番号順)。章の中では次のプレースホルダで
#  実際のソースコードを引用できる (書き写しの誤りを防ぐため必ず実物から抽出する):
#    {{fn:shaders/sph.hlsl:CS_Density}}          関数 1 つ (直前のコメントを含む)
#    {{range:src/main.cpp|開始行の文字列|終了行の文字列}}  行範囲 (区切りは |)
#    {{file:src/SimConfig.h}}                   ファイル全文 (行番号付き)
#    {{fig:kernels.png:キャプション}}            図 (book/figures/)
# =====================================================================
import io, os, re, glob, html, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOOK = os.path.join(ROOT, 'book')
CHAPTERS = os.path.join(BOOK, 'chapters')
OUT_HTML = os.path.join(BOOK, 'CG解説書.html')
OUT_PDF = os.path.join(BOOK, 'CG解説書.pdf')
CHROME = r'C:\Program Files\Google\Chrome\Application\chrome.exe'

fig_counter = [0]
listing_counter = [0]

def read(path):
    return io.open(os.path.join(ROOT, path), encoding='utf-8').read().replace('\r\n', '\n')

def lang_of(path):
    return 'hlsl' if path.endswith(('.hlsl', '.hlsli')) else 'cpp'

def code_block(text, path, first_line=1, title=None):
    listing_counter[0] += 1
    lines = text.split('\n')
    numbered = '\n'.join(f'{first_line + i:4d}  {html.escape(l)}' for i, l in enumerate(lines))
    cap = f'リスト {listing_counter[0]}: {html.escape(path)}' + (f' — {html.escape(title)}' if title else '')
    return (f'<figure class="listing"><figcaption>{cap}</figcaption>'
            f'<pre><code class="language-{lang_of(path)}">{numbered}</code></pre></figure>')

def extract_function(path, name):
    """関数/メソッド本体を直前のコメント・属性ごと抜き出す"""
    lines = read(path).split('\n')
    pat = re.compile(r'\b' + re.escape(name) + r'\s*\(')
    start = None
    for i, l in enumerate(lines):
        if pat.search(l) and not l.rstrip().endswith(';') and not l.lstrip().startswith('//'):
            start = i; break
    if start is None:
        raise SystemExit(f'function not found: {path} {name}')
    # 直前のコメント行・属性行を含める
    s = start
    while s > 0 and (lines[s-1].lstrip().startswith('//') or lines[s-1].lstrip().startswith('[')):
        s -= 1
    # 波括弧の深さで終端を探す
    depth = 0; started = False; e = start
    for i in range(start, len(lines)):
        for ch in lines[i]:
            if ch == '{': depth += 1; started = True
            elif ch == '}': depth -= 1
        if started and depth == 0:
            e = i; break
    return '\n'.join(lines[s:e+1]), s + 1

def extract_range(path, a, b):
    lines = read(path).split('\n')
    s = next(i for i, l in enumerate(lines) if a in l)
    e = next(i for i in range(s, len(lines)) if b in lines[i])
    return '\n'.join(lines[s:e+1]), s + 1

def expand(chapter_html):
    """プレースホルダを文書中の出現順に展開する (番号が本文の順序と一致するよう 1 パスで行う)"""
    def fig_html(name, cap):
        fig_counter[0] += 1
        return (f'<figure class="fig"><img src="figures/{name}" alt="{html.escape(cap)}">'
                f'<figcaption>図 {fig_counter[0]}: {cap}</figcaption></figure>')
    def dispatch(m):
        kind, arg = m.group(1), m.group(2)
        if kind == 'fn':
            path, name = arg.split(':', 1)
            text, first = extract_function(path, name)
            return code_block(text, path, first, name)
        if kind == 'range':
            path, a, b = arg.split('|', 2)
            text, first = extract_range(path, a, b)
            return code_block(text, path, first)
        if kind == 'file':
            return code_block(read(arg).rstrip(chr(10)), arg, 1)
        name, cap = arg.split(':', 1)
        return fig_html(name, cap)
    return re.sub(r'\{\{(fn|range|file|fig):(.+?)\}\}', dispatch, chapter_html)

def build_toc(body):
    """h1/h2 から目次を作る (id を自動付与)"""
    entries = []
    counter = [0]
    def repl(m):
        level, attrs, text = m.group(1), m.group(2), m.group(3)
        counter[0] += 1
        idm = re.search(r'id="([^"]+)"', attrs)
        if idm:
            hid = idm.group(1)
        else:
            hid = f'h{counter[0]}'
            attrs = attrs + f' id="{hid}"'
        plain = re.sub(r'<[^>]+>', '', text.replace('</span>', '</span> '))
        entries.append((level, hid, plain))
        return f'<h{level}{attrs}>{text}</h{level}>'
    body = re.sub(r'<h([12])([^>]*)>(.*?)</h\1>', repl, body, flags=re.S)
    toc = ['<nav class="toc"><h1 class="toc-title">目次</h1><ol>']
    for level, hid, text in entries:
        toc.append(f'<li class="lv{level}"><a href="#{hid}">{html.escape(text)}</a></li>')
    toc.append('</ol></nav>')
    return body, '\n'.join(toc)

def main():
    parts = []
    for path in sorted(glob.glob(os.path.join(CHAPTERS, '*.html'))):
        parts.append(expand(io.open(path, encoding='utf-8').read()))
    body = '\n'.join(parts)
    body, toc = build_toc(body)

    template = io.open(os.path.join(BOOK, 'template.html'), encoding='utf-8').read()
    out = template.replace('{{TOC}}', toc).replace('{{BODY}}', body)
    io.open(OUT_HTML, 'w', encoding='utf-8', newline='\n').write(out)
    print('html:', OUT_HTML, f'({len(out)//1024} KB, figures={fig_counter[0]}, listings={listing_counter[0]})')

    if '--no-pdf' in sys.argv:
        return
    url = 'file:///' + OUT_HTML.replace('\\', '/')
    cmd = [CHROME, '--headless=new', '--disable-gpu', '--no-pdf-header-footer',
           '--virtual-time-budget=90000', '--run-all-compositor-stages-before-draw',
           f'--print-to-pdf={OUT_PDF}', url]
    subprocess.run(cmd, check=True, capture_output=True)
    print('pdf :', OUT_PDF, f'({os.path.getsize(OUT_PDF)//1024} KB)')

if __name__ == '__main__':
    main()
