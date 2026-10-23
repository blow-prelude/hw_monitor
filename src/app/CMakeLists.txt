# 可执行 hw_monitor：探测 + 依赖注入 + 启动（DESIGN §9）。
# 接口与实现的绑定只发生在这里（依赖注入），各层可独立替换。
add_executable(hw_monitor
    main.cpp
)
target_link_libraries(hw_monitor PRIVATE
    hw_collectors
    hw_tui
)
