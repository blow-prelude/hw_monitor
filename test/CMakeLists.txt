# 核心层单测（doctest，header-only，vendored 于 test/doctest.h —— 01-core §10）
add_executable(hw_core_tests
    main.cpp
    ring_buffer_test.cpp
    monitor_hub_test.cpp
)
target_include_directories(hw_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hw_core_tests PRIVATE hw_core)
add_test(NAME core COMMAND hw_core_tests)
