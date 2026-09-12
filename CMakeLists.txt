# =====================================================================
#  CMakeLists.txt  --  Visual Studio ジェネレータ用 (build.bat の代替)
#    cmake -S . -B build_cmake -G "Visual Studio 17 2022" -A x64
#    cmake --build build_cmake --config Release
#  shaders\ は実行ファイルの隣にコピーされる。
# =====================================================================
cmake_minimum_required(VERSION 3.20)
project(SPHSplash LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(/utf-8 /W3)
add_compile_definitions(UNICODE _UNICODE)

# 本体
add_executable(SPHSplash
    src/main.cpp src/D3DUtil.cpp src/SPHSimulator.cpp
    src/MarchingCubes.cpp src/MCTables.cpp src/Renderer.cpp)
target_link_libraries(SPHSplash PRIVATE d3d11 dxgi d3dcompiler user32)
set_target_properties(SPHSplash PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")

# シェーダーを出力ディレクトリへコピー
add_custom_command(TARGET SPHSplash POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:SPHSplash>/shaders)

# マーチングキューブ三角形テーブルの単体テスト
add_executable(mc_selftest src/mc_selftest.cpp src/MCTables.cpp)
