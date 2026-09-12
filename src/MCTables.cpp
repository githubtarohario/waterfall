// =====================================================================
//  MCTables.cpp
//  マーチングキューブ法の三角形テーブル生成と自己検証 (詳細は MCTables.h)
// =====================================================================
#include "MCTables.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <tuple>

namespace mc
{
    namespace
    {
        struct Vec3 { float x, y, z; };
        static Vec3 Sub(Vec3 a, Vec3 b)   { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        static Vec3 Cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
        static float Dot(Vec3 a, Vec3 b)  { return a.x * b.x + a.y * b.y + a.z * b.z; }

        // 頂点番号 → 単位立方体内の座標 (ビットを座標に展開)
        static Vec3 Corner(int v) { return { float(v & 1), float((v >> 1) & 1), float((v >> 2) & 1) }; }

        // 立方体の 6 面。各面を構成する 4 頂点を「隣り合う順」に列挙 (向きは後で整える)
        constexpr int FACE_VERTS[6][4] = {
            { 0, 2, 6, 4 }, // -x 面
            { 1, 3, 7, 5 }, // +x 面
            { 0, 1, 5, 4 }, // -y 面
            { 2, 3, 7, 6 }, // +y 面
            { 0, 1, 3, 2 }, // -z 面
            { 4, 5, 7, 6 }, // +z 面
        };

        // 2 頂点を結ぶエッジ番号を返す (存在しなければ -1)
        static int EdgeBetween(int a, int b)
        {
            for (int e = 0; e < 12; ++e)
            {
                if ((EDGE_VERTS[e][0] == a && EDGE_VERTS[e][1] == b) ||
                    (EDGE_VERTS[e][0] == b && EDGE_VERTS[e][1] == a))
                    return e;
            }
            return -1;
        }

        // 面の 4 頂点を「立方体の外側から見て反時計回り」になるよう並べ替える
        static void OrientedFace(int f, int out[4])
        {
            for (int k = 0; k < 4; ++k) out[k] = FACE_VERTS[f][k];
            Vec3 p0 = Corner(out[0]), p1 = Corner(out[1]), p2 = Corner(out[2]);
            Vec3 n = Cross(Sub(p1, p0), Sub(p2, p0));         // 現在の並びでの法線
            Vec3 center = { 0, 0, 0 };
            for (int k = 0; k < 4; ++k) { Vec3 c = Corner(out[k]); center.x += c.x * 0.25f; center.y += c.y * 0.25f; center.z += c.z * 0.25f; }
            Vec3 outward = Sub(center, Vec3{ 0.5f, 0.5f, 0.5f });   // 立方体中心から面中心へ = 外向き
            if (Dot(n, outward) < 0.0f) std::swap(out[1], out[3]);  // 逆回りなら反転
        }

        // エッジの中点 (ループの向き判定用の代表点)
        static Vec3 EdgeMid(int e)
        {
            Vec3 a = Corner(EDGE_VERTS[e][0]), b = Corner(EDGE_VERTS[e][1]);
            return { (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f };
        }
    }

    std::vector<int32_t> GenerateTriangleTable()
    {
        std::vector<int32_t> table(NUM_CASES * TABLE_STRIDE, -1);

        // 面ごとの向き付き頂点列を先に作っておく
        int faces[6][4];
        for (int f = 0; f < 6; ++f) OrientedFace(f, faces[f]);

        for (int mask = 0; mask < NUM_CASES; ++mask)
        {
            auto inside = [&](int v) { return (mask >> v) & 1; };

            // next[e] : 交差エッジ e から同じ面上で接続される次の交差エッジ
            int next[12];
            for (int e = 0; e < 12; ++e) next[e] = -1;

            for (int f = 0; f < 6; ++f)
            {
                // 面の境界を反時計回りに歩き、内外が変わるエッジを順に集める
                // type: true = 入口 (外→内), false = 出口 (内→外)
                struct Hit { int edge; bool entry; };
                Hit seq[4]; int n = 0;
                for (int k = 0; k < 4; ++k)
                {
                    int a = faces[f][k], b = faces[f][(k + 1) % 4];
                    if (inside(a) != inside(b))
                        seq[n++] = { EdgeBetween(a, b), inside(b) != 0 };
                }
                // 入口から (周回方向で) 次の出口へ接続する。
                // 4 交差の曖昧面でもこの規則は面の頂点値だけで決まるので隣接セルと必ず一致する。
                for (int i = 0; i < n; ++i)
                {
                    if (!seq[i].entry) continue;
                    int j = (i + 1) % n;          // 入口と出口は交互に現れるので次は必ず出口
                    next[seq[i].edge] = seq[j].edge;
                }
            }

            // 有向リンクを辿って閉ループを取り出し、扇状に三角形化する
            bool visited[12] = {};
            int  writePos = 0;
            for (int start = 0; start < 12; ++start)
            {
                if (next[start] < 0 || visited[start]) continue;

                std::vector<int> loop;
                int e = start;
                do { loop.push_back(e); visited[e] = true; e = next[e]; } while (e != start && loop.size() <= 12);

                // ループの向きを「法線が流体の外側 (値の低い側) を向く」よう統一する
                Vec3 normal = { 0, 0, 0 }, centroid = { 0, 0, 0 }, fluid = { 0, 0, 0 };
                for (size_t i = 0; i < loop.size(); ++i)
                {
                    Vec3 p = EdgeMid(loop[i]), q = EdgeMid(loop[(i + 1) % loop.size()]);
                    Vec3 c = Cross(p, q);                                    // Newell 法
                    normal.x += c.x; normal.y += c.y; normal.z += c.z;
                    centroid.x += p.x; centroid.y += p.y; centroid.z += p.z;
                    int a = EDGE_VERTS[loop[i]][0], b = EDGE_VERTS[loop[i]][1];
                    Vec3 in = Corner(inside(a) ? a : b);                     // このエッジの内側端点
                    fluid.x += in.x; fluid.y += in.y; fluid.z += in.z;
                }
                float inv = 1.0f / float(loop.size());
                Vec3 toFluid = { fluid.x * inv - centroid.x * inv, fluid.y * inv - centroid.y * inv, fluid.z * inv - centroid.z * inv };
                if (Dot(normal, toFluid) > 0.0f)                            // 法線が流体側を向いていたら反転
                    std::reverse(loop.begin(), loop.end());

                // 扇状三角形分割
                for (size_t i = 1; i + 1 < loop.size(); ++i)
                {
                    if (writePos + 3 >= TABLE_STRIDE) { std::fprintf(stderr, "MC table overflow at case %d\n", mask); break; }
                    table[mask * TABLE_STRIDE + writePos + 0] = loop[0];
                    table[mask * TABLE_STRIDE + writePos + 1] = loop[i];
                    table[mask * TABLE_STRIDE + writePos + 2] = loop[i + 1];
                    writePos += 3;
                }
            }
            // 残りは -1 (終端) のまま
        }
        return table;
    }

    bool SelfTest(const std::vector<int32_t>& table, int32_t* outTriangleCount)
    {
        // 球の符号付き距離場を格子上にサンプリングし CPU で MC を実行する
        const int N = 20;                     // セル数 (各軸)
        const float cx = 10.31f, cy = 9.77f, cz = 10.13f, radius = 7.3f;
        auto field = [&](int x, int y, int z) {
            float dx = x - cx, dy = y - cy, dz = z - cz;
            return radius - std::sqrt(dx * dx + dy * dy + dz * dz);   // 内側が正
        };

        // エッジ → 頂点番号 (隣接セルで同じエッジは同じ頂点になるよう正規化キーで管理)
        std::map<std::tuple<int, int, int, int>, int> vertexIds;
        std::vector<Vec3> vertices;
        std::vector<std::array<int, 3>> tris;

        for (int z = 0; z < N; ++z) for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x)
        {
            float v[8]; int mask = 0;
            for (int k = 0; k < 8; ++k)
            {
                v[k] = field(x + (k & 1), y + ((k >> 1) & 1), z + ((k >> 2) & 1));
                if (v[k] > 0.0f) mask |= 1 << k;
            }
            if (mask == 0 || mask == 255) continue;

            auto vertexOnEdge = [&](int e) {
                int a = EDGE_VERTS[e][0], b = EDGE_VERTS[e][1];
                Vec3 pa = Corner(a), pb = Corner(b);
                auto key = std::make_tuple(x + int(pa.x), y + int(pa.y), z + int(pa.z), e / 4);
                auto it = vertexIds.find(key);
                if (it != vertexIds.end()) return it->second;
                float t = (0.0f - v[a]) / (v[b] - v[a]);
                Vec3 p = { x + pa.x + (pb.x - pa.x) * t, y + pa.y + (pb.y - pa.y) * t, z + pa.z + (pb.z - pa.z) * t };
                int id = int(vertices.size());
                vertices.push_back(p);
                vertexIds[key] = id;
                return id;
            };

            for (int k = 0; k < TABLE_STRIDE; k += 3)
            {
                int e0 = table[mask * TABLE_STRIDE + k];
                if (e0 < 0) break;
                int e1 = table[mask * TABLE_STRIDE + k + 1];
                int e2 = table[mask * TABLE_STRIDE + k + 2];
                tris.push_back({ vertexOnEdge(e0), vertexOnEdge(e1), vertexOnEdge(e2) });
            }
        }

        // 検証 1: 各有向エッジがちょうど 1 回、逆向きもちょうど 1 回 (閉じた 2 多様体)
        std::map<std::pair<int, int>, int> edgeUse;
        for (auto& t : tris)
            for (int k = 0; k < 3; ++k)
                edgeUse[{ t[k], t[(k + 1) % 3] }]++;
        bool ok = true;
        for (auto& kv : edgeUse)
        {
            if (kv.second != 1) { ok = false; break; }
            auto rev = edgeUse.find({ kv.first.second, kv.first.first });
            if (rev == edgeUse.end() || rev->second != 1) { ok = false; break; }
        }
        // 検証 2: 三角形の幾何法線が外向き (球の中心から遠ざかる向き)
        int badOrient = 0;
        for (auto& t : tris)
        {
            Vec3 p0 = vertices[t[0]], p1 = vertices[t[1]], p2 = vertices[t[2]];
            Vec3 n = Cross(Sub(p1, p0), Sub(p2, p0));
            Vec3 c = { (p0.x + p1.x + p2.x) / 3 - cx, (p0.y + p1.y + p2.y) / 3 - cy, (p0.z + p1.z + p2.z) / 3 - cz };
            if (Dot(n, c) <= 0.0f) ++badOrient;
        }
        if (badOrient > 0) ok = false;
        if (outTriangleCount) *outTriangleCount = int32_t(tris.size());
        std::printf("[MC self test] triangles=%zu manifold=%s badOrientation=%d\n", tris.size(), ok ? "yes" : "NO", badOrient);
        return ok;
    }
}
