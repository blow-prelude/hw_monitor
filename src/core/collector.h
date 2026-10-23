#pragma once
// 采集器接口契约（01-core §4）。六个域接口为纯虚类，采集层实现、
// core（MonitorHub）调度、显示层不可见具体采集器类型。

#include <vector>

#include "core/snapshot.h"

namespace hwmon {

// 所有采集器的根接口。
class ICollector {
public:
    virtual ~ICollector() = default;
    virtual const char* domain() const = 0;  // "cpu"/"gpu"/"npu"/"mem"/"disk"/"net"
    virtual bool probe() = 0;                // 只读探测；启动阶段由 main 调一次
    // 注意：sample() / info() 属于各域子接口，不放在根接口（避免 void* 泄漏类型）
};

// 实现纪律（01-core §4）：
//  - sample() 不得抛异常、不得阻塞超过轮询周期（当前 1s）；读文件失败一律
//    落为 valid=false / 条目缺失。
//  - info() 可被多次调用且必须幂等；建议 probe 时缓存。
//  - 采集器对象只被采集线程访问（hub 保证），无需自身加锁。

class ICpuCollector : public ICollector {
public:
    virtual CpuInfo   info() const = 0;  // 静态信息，probe() 后即可调用，结果可缓存
    virtual CpuSample sample() = 0;      // 动态快照；不可得项 valid=false
};

class IGpuCollector : public ICollector {
public:
    virtual GpuInfo info() const = 0;
    virtual std::vector<GpuSample> sample() = 0;  // 一次覆盖所有卡，与 GpuInfo::cards 一一对应
};

class INpuCollector : public ICollector {
public:
    virtual NpuInfo   info() const = 0;
    virtual NpuSample sample() = 0;
};

class IMemCollector : public ICollector {
public:
    virtual MemSample sample() = 0;
};

class IDiskCollector : public ICollector {
public:
    virtual DiskSample sample() = 0;
};

class INetCollector : public ICollector {
public:
    virtual NetSample sample() = 0;
};

}  // namespace hwmon
