#pragma once
#include "goldhen_sdk.h"
#include <cstdint>
#include <mutex>
#include <string>

namespace mc {
enum class UsbAccessStage {
    NotAttempted, SdkUnavailable, UnsupportedSdk, EnableFailed,
    Enabled, RestoreFailed, Restored
};
struct UsbAccessStatus {
    UsbAccessStage stage = UsbAccessStage::NotAttempted;
    uint32_t sdk_version = 0;
    int result = 0, error = 0;
    bool active = false;
    std::string description() const;
};

class DirectUsbAccess {
public:
    using Command = int (*)(uint64_t, void*);
    explicit DirectUsbAccess(Command command = mc_goldhen_sdk_command) : command_(command) {}
    DirectUsbAccess(const DirectUsbAccess&) = delete;
    DirectUsbAccess& operator=(const DirectUsbAccess&) = delete;
    bool prepare();
    bool restore();
    UsbAccessStatus status() const;
private:
    Command command_;
    mutable std::mutex mutex_;
    McGoldhenBackup backup_{};
    UsbAccessStatus status_{};
};

bool prepare_direct_usb_access();
UsbAccessStatus direct_usb_access_status();
// Declare before the platform/application so their workers and handles have
// been released before the original process namespace is restored.
class DirectUsbAccessScope {
public:
    ~DirectUsbAccessScope();
};
}
