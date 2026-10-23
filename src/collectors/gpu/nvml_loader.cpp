#include "collectors/gpu/nvml_loader.h"

#include <dlfcn.h>

namespace hwmon::nvml {
namespace {

// dlsym 取符号并判空；任一缺失 → 整个 NVIDIA 路径降级。
template <typename Fn>
Fn symOrNull(void* handle, const char* name) {
    void* p = ::dlsym(handle, name);
    return p ? reinterpret_cast<Fn>(p) : nullptr;
}

}  // namespace

NvmlLoader::NvmlLoader() : init(nullptr), shutdown(nullptr), getCount(nullptr),
                           getHandleByIndex(nullptr), getName(nullptr),
                           getUtilizationRates(nullptr), getClockInfo(nullptr),
                           getMaxClockInfo(nullptr), getMemoryInfo(nullptr),
                           getTemperature(nullptr) {
    // 只读加载厂商库；失败静默降级（CLAUDE.md：厂商组件缺失不得导致启动崩溃）。
    handle_ = ::dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!handle_) return;

    init               = symOrNull<int (*)()>(handle_, "nvmlInit_v2");
    shutdown           = symOrNull<int (*)()>(handle_, "nvmlShutdown");
    getCount           = symOrNull<int (*)(unsigned int*)>(handle_, "nvmlDeviceGetCount_v2");
    getHandleByIndex   = symOrNull<int (*)(unsigned int, nvmlDevice_t*)>(handle_, "nvmlDeviceGetHandleByIndex_v2");
    getName            = symOrNull<int (*)(nvmlDevice_t, char*, unsigned int)>(handle_, "nvmlDeviceGetName");
    getUtilizationRates= symOrNull<int (*)(nvmlDevice_t, nvmlUtilization_t*)>(handle_, "nvmlDeviceGetUtilizationRates");
    getClockInfo       = symOrNull<int (*)(nvmlDevice_t, int, unsigned int*)>(handle_, "nvmlDeviceGetClockInfo");
    getMaxClockInfo    = symOrNull<int (*)(nvmlDevice_t, int, unsigned int*)>(handle_, "nvmlDeviceGetMaxClockInfo");
    getMemoryInfo      = symOrNull<int (*)(nvmlDevice_t, nvmlMemory_t*)>(handle_, "nvmlDeviceGetMemoryInfo");
    getTemperature     = symOrNull<int (*)(nvmlDevice_t, int, unsigned int*)>(handle_, "nvmlDeviceGetTemperature");

    if (!init || !shutdown || !getCount || !getHandleByIndex || !getName ||
        !getUtilizationRates || !getClockInfo || !getMaxClockInfo ||
        !getMemoryInfo || !getTemperature) {
        ok_ = false;
        return;  // handle_ 析构时 dlclose
    }
    ok_ = (init() == NVML_SUCCESS);  // 库初始化（query 类，不改硬件状态）
}

NvmlLoader::~NvmlLoader() {
    if (ok_ && shutdown) shutdown();
    if (handle_) ::dlclose(handle_);
}

}  // namespace hwmon::nvml
