#pragma once
// libnvidia-ml.so.1 的 dlopen 封装（02-collectors §8.2）。
// 库缺失/符号缺失/init 失败一律静默降级（ok()==false），不抛异常。
#include "collectors/gpu/nvml_types.h"

namespace hwmon::nvml {

class NvmlLoader {
public:
    NvmlLoader();   // dlopen + dlsym 全符号表 + nvmlInit_v2
    ~NvmlLoader();  // nvmlShutdown + dlclose（RAII；全进程至多一实例，由 GpuCollector 持有）

    NvmlLoader(const NvmlLoader&) = delete;
    NvmlLoader& operator=(const NvmlLoader&) = delete;

    bool ok() const { return ok_; }

    // query 类函数指针；ok()==false 时为空，调用方以 ok() 先行判断。
    int (*init)();                                            // nvmlInit_v2
    int (*shutdown)();                                        // nvmlShutdown
    int (*getCount)(unsigned int*);                           // nvmlDeviceGetCount_v2
    int (*getHandleByIndex)(unsigned int, nvmlDevice_t*);     // nvmlDeviceGetHandleByIndex_v2
    int (*getName)(nvmlDevice_t, char*, unsigned int);        // nvmlDeviceGetName
    int (*getUtilizationRates)(nvmlDevice_t, nvmlUtilization_t*);
    int (*getClockInfo)(nvmlDevice_t, int, unsigned int*);
    int (*getMaxClockInfo)(nvmlDevice_t, int, unsigned int*);
    int (*getMemoryInfo)(nvmlDevice_t, nvmlMemory_t*);
    int (*getTemperature)(nvmlDevice_t, int, unsigned int*);

private:
    void* handle_ = nullptr;
    bool ok_ = false;
};

}  // namespace hwmon::nvml
