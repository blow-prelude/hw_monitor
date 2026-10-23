#pragma once
// NVML 最小 C 布局自持定义（02-collectors §8.2：不引入 NVIDIA 头文件）。
// 布局源自 NVML 公开文档，仅覆盖本项目用到的 query API。
#include <cstddef>

namespace hwmon::nvml {

typedef struct nvmlDevice_s* nvmlDevice_t;

// nvmlReturn_t（只用到 SUCCESS）
enum NvmlReturn { NVML_SUCCESS = 0 };

// 时钟域 / 温度传感器常量
enum { NVML_CLOCK_SM = 1 };          // 0=GRAPHICS, 1=SM, 2=MEM
enum { NVML_TEMPERATURE_GPU = 0 };

struct nvmlUtilization_t {
    unsigned int gpu;
    unsigned int memory;
};

struct nvmlMemory_t {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

}  // namespace hwmon::nvml
