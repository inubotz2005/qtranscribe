#pragma once

namespace keyinjectord {

class IRawDevice {
public:
    virtual ~IRawDevice() = default;
    virtual bool emitEvent(int type, int code, int value) = 0;
};

class IDevice {
public:
    virtual ~IDevice() = default;
    virtual bool sendCtrlV() = 0;
};

} // namespace keyinjectord
