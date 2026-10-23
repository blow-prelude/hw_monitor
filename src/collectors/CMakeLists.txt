# 采集层静态库（02-collectors §2）：链接 hw_core 与 dl（NVML dlopen 用）。
add_library(hw_collectors STATIC
    util.cpp
    cpu/CpuCollector.cpp
    cpu/arm_model.cpp
    mem/MemCollector.cpp
    disk/DiskCollector.cpp
    net/NetCollector.cpp
    gpu/GpuCollector.cpp
    gpu/nvml_loader.cpp
    gpu/amd_drm.cpp
    npu/NpuCollector.cpp
)
target_link_libraries(hw_collectors PUBLIC hw_core ${CMAKE_DL_LIBS})
