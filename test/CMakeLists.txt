# —— 核心层单测（doctest，vendored 单头文件于本目录 —— 01-core §10）
add_executable(hw_core_tests
    main.cpp
    ring_buffer_test.cpp
    monitor_hub_test.cpp
)
target_include_directories(hw_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hw_core_tests PRIVATE hw_core)
add_test(NAME core COMMAND hw_core_tests)

# —— 采集层单测：解析纯函数 × test/fixtures 静态样本（02-collectors）——
add_executable(hw_unit_tests
    test_main.cpp
    test_util.cpp
    test_cpu.cpp
    test_mem.cpp
    test_net.cpp
    test_disk.cpp
    test_npu.cpp
    test_gpu.cpp
)
target_include_directories(hw_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})  # doctest.h
target_compile_definitions(hw_unit_tests PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(hw_unit_tests PRIVATE hw_collectors)
add_test(NAME collectors COMMAND hw_unit_tests)

# 真机冒烟工具：读本机 /proc、/sys 实时数据，人工验收（不入 ctest）
add_executable(collectors_smoke smoke_collectors.cpp)
target_link_libraries(collectors_smoke PRIVATE hw_collectors)
