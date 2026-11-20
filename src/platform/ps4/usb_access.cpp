#include "usb_access.hpp"
#include "startup_trace.hpp"
#include "GoldHEN.h"
#include <cerrno>
#include <cstddef>
#include <cstdio>

static_assert(sizeof(McGoldhenBackup) == sizeof(jailbreak_backup), "GoldHEN backup ABI changed");
static_assert(sizeof(McGoldhenBackup) == 72, "GoldHEN requires the 64-bit backup ABI");
static_assert(MC_GOLDHEN_SDK_VERSION == GOLDHEN_SDK_VERSION, "GoldHEN version reply changed");
#define MC_CHECK_BACKUP_FIELD(name) \
    static_assert(offsetof(McGoldhenBackup, name) == offsetof(jailbreak_backup, name), "GoldHEN field ABI changed")
MC_CHECK_BACKUP_FIELD(cr_uid);
MC_CHECK_BACKUP_FIELD(cr_ruid);
MC_CHECK_BACKUP_FIELD(cr_rgid);
MC_CHECK_BACKUP_FIELD(cr_groups);
MC_CHECK_BACKUP_FIELD(cr_paid);
MC_CHECK_BACKUP_FIELD(cr_caps);
MC_CHECK_BACKUP_FIELD(cr_prison);
MC_CHECK_BACKUP_FIELD(fd_cdir);
MC_CHECK_BACKUP_FIELD(fd_jdir);
MC_CHECK_BACKUP_FIELD(fd_rdir);
#undef MC_CHECK_BACKUP_FIELD
static_assert(MC_GOLDHEN_VERSION == GOLDHEN_SDK_CMD_VERSION
    && MC_GOLDHEN_JAILBREAK == GOLDHEN_SDK_CMD_JAILBREAK
    && MC_GOLDHEN_UNJAILBREAK == GOLDHEN_SDK_CMD_UNJAILBREAK, "GoldHEN command ABI changed");

namespace mc {
namespace {
struct PreserveErrno {
    int saved = errno;
    ~PreserveErrno() { errno = saved; }
};
DirectUsbAccess& process_access() {
    static DirectUsbAccess access;
    return access;
}
}

std::string UsbAccessStatus::description() const {
    const char* operation = "not attempted";
    switch (stage) {
        case UsbAccessStage::SdkUnavailable: operation = "GoldHEN SDK unavailable"; break;
        case UsbAccessStage::UnsupportedSdk: operation = "unsupported GoldHEN SDK ABI"; break;
        case UsbAccessStage::EnableFailed: operation = "GoldHEN could not enable process access"; break;
        case UsbAccessStage::Enabled: operation = "GoldHEN process access enabled"; break;
        case UsbAccessStage::RestoreFailed: operation = "GoldHEN could not restore process access"; break;
        case UsbAccessStage::Restored: operation = "original process access restored"; break;
        default: break;
    }
    char detail[128];
    std::snprintf(detail, sizeof(detail), " (SDK=0x%08x, result=%d, errno=%d)", sdk_version, result, error);
    return std::string("Direct USB, native read-only files | ") + operation + detail;
}

bool DirectUsbAccess::prepare() {
    PreserveErrno original;
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.active) return true;
    status_ = {};
    errno = 0;
    int version = command_(MC_GOLDHEN_VERSION, nullptr);
    if (version < 0) {
        status_.stage = UsbAccessStage::SdkUnavailable;
        status_.result = version;
        status_.error = errno ? errno : ENOSYS;
        startup_trace("usb.direct.sdk-unavailable", version);
        return false;
    }
    status_.sdk_version = static_cast<uint32_t>(version);
    // A different major SDK ABI must not receive our credential-backup buffer.
    if ((status_.sdk_version & 0xffffff00u) != GOLDHEN_SDK_VERSION) {
        status_.stage = UsbAccessStage::UnsupportedSdk;
        status_.result = version;
        status_.error = EPROTONOSUPPORT;
        startup_trace("usb.direct.sdk-unsupported", version);
        return false;
    }
    backup_ = {};
    errno = 0;
    startup_trace("usb.direct.enable.begin", version);
    status_.result = command_(MC_GOLDHEN_JAILBREAK, &backup_);
    if (status_.result != 0) {
        status_.stage = UsbAccessStage::EnableFailed;
        status_.error = errno ? errno : EIO;
        startup_trace("usb.direct.enable.failed", status_.result);
        return false;
    }
    status_.stage = UsbAccessStage::Enabled;
    status_.active = true;
    startup_trace("usb.direct.enable.ready", version);
    return true;
}

bool DirectUsbAccess::restore() {
    PreserveErrno original;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!status_.active) return true;
    errno = 0;
    status_.result = command_(MC_GOLDHEN_UNJAILBREAK, &backup_);
    status_.error = status_.result == 0 ? 0 : (errno ? errno : EIO);
    status_.stage = status_.result == 0 ? UsbAccessStage::Restored : UsbAccessStage::RestoreFailed;
    startup_trace("usb.direct.restore", status_.result);
    if (status_.result == 0) { status_.active = false; backup_ = {}; }
    return !status_.active;
}

UsbAccessStatus DirectUsbAccess::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}
bool prepare_direct_usb_access() { return process_access().prepare(); }
UsbAccessStatus direct_usb_access_status() { return process_access().status(); }
DirectUsbAccessScope::~DirectUsbAccessScope() { process_access().restore(); }
}
