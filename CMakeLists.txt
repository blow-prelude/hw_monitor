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

# vendored FTXUI（勿修改；关闭其文档/示例/测试与安装）。
# 注：其 cmake_minimum_required 3.28.2 仅在 FTXUI_BUILD_MODULES=ON 时生效，
# 默认 OFF，本机 cmake 3.22.1 可直接构建。
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/FTXUI)

# 核心层：接口契约 + 数据模型 + MonitorHub（不链接 FTXUI、不链接 dl）
add_subdirectory(src/core)

# 采集层：只读 procfs/sysfs/NVML(dlopen)，链接 hw_core 与 dl
add_subdirectory(src/collectors)

# 显示层：组件树/进度条/折线图，链接 hw_core 与 FTXUI
add_subdirectory(src/tui)

# 主程序：探测 + 依赖注入 + 启动
add_subdirectory(src/app)

enable_testing()
add_subdirectory(test)
