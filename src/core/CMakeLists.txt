# 核心层静态库（01-core §2）：collector.h / snapshot.h / ring_buffer.h 仅头文件，
# monitor_hub 为唯一编译单元。对外暴露 src/ 为 include 根（#include "core/xxx.h"）。
add_library(hw_core STATIC
    monitor_hub.cpp
)
target_include_directories(hw_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(hw_core PUBLIC Threads::Threads)
target_compile_options(hw_core PRIVATE -Wall -Wextra -Wpedantic)
