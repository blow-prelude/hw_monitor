cmake_minimum_required(VERSION 3.16)
project(hw_monitor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)

# 核心层：接口契约 + 数据模型 + MonitorHub（不链接 FTXUI、不链接 dl）
add_subdirectory(src/core)

# 采集层：只读 procfs/sysfs/NVML(dlopen)，链接 hw_core 与 dl
add_subdirectory(src/collectors)

enable_testing()
add_subdirectory(test)

# 3rdparty/FTXUI 供显示层（src/tui，design/03-tui.md）引入；当前 vendored 副本的
# cmake_minimum_required 为 3.28.2，高于本机 3.22.1，故此处暂不 add_subdirectory，
# 待显示层落地（或 cmake 升级）时再接入。
