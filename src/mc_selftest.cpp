// =====================================================================
//  mc_selftest.cpp
//  マーチングキューブ三角形テーブル生成の単体検証プログラム (コンソール)
//  build.bat で mc_selftest.exe としてビルドされる。終了コード 0 = 合格。
// =====================================================================
#include "MCTables.h"
#include <cstdio>

int main()
{
    std::vector<int32_t> table = mc::GenerateTriangleTable();

    // パターンごとの三角形数の統計を表示
    int maxTris = 0;
    for (int c = 0; c < mc::NUM_CASES; ++c)
    {
        int n = 0;
        while (n < mc::TABLE_STRIDE && table[c * mc::TABLE_STRIDE + n] >= 0) n += 3;
        if (n / 3 > maxTris) maxTris = n / 3;
    }
    std::printf("max triangles per cell = %d\n", maxTris);

    int32_t triCount = 0;
    bool ok = mc::SelfTest(table, &triCount);
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
