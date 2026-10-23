# 显示层：组件树、进度条/折线图、各节渲染。静态库 hw_tui，
# 链接 hw_core 与 FTXUI（03-tui.md §2）。
# 依赖方向：app_ui → sections → widgets/format → core 视图类型；
# 只 include FTXUI 公共头与 core/snapshot.h，禁止 include 任何 collectors 头。
add_library(hw_tui STATIC
    format.cpp
    widgets/progress_bar.cpp
    widgets/line_chart.cpp
    sections/cpu_section.cpp
    sections/gpu_section.cpp
    sections/npu_section.cpp
    sections/mem_disk_section.cpp
    sections/net_section.cpp
    app_ui.cpp
)
add_library(hwmon::tui ALIAS hw_tui)

target_include_directories(hw_tui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)  # src/，供 #include "tui/…"
target_compile_features(hw_tui PUBLIC cxx_std_17)
target_link_libraries(hw_tui PUBLIC
    hw_core
    ftxui::component
    ftxui::dom
    ftxui::screen
)
