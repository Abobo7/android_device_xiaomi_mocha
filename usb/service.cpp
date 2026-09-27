/*
 * Simplified USB port HAL for Xiaomi Mi Pad (mocha).
 *
 * mocha is a micro-USB device with no Type-C port controller: the stock
 * 3.10 kernel exposes the legacy android_usb gadget only and has no
 * /sys/class/dual_role_usb nodes, so the AOSP Type-C implementation
 * cannot be used. Android 9's UsbPortManager still requires an IUsb HAL
 * to publish a UsbPort; without it Settings disables the whole USB
 * function chooser (file transfer / PTP / tethering / charging).
 *
 * This HAL reports one non-switchable port whose roles are fixed
 * (data = device, power = sink, mode = UFP when the gadget is
 * connected). Connection state is read live from the legacy gadget
 * sysfs so the port appears connected exactly while USB is up.
 */

#include <android/hardware/usb/1.0/IUsb.h>
#include <android/hardware/usb/1.0/IUsbCallback.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "android.hardware.usb@1.0-service"

using ::android::hardware::usb::V1_0::IUsb;
using ::android::hardware::usb::V1_0::IUsbCallback;
using ::android::hardware::usb::V1_0::PortRole;
using ::android::hardware::usb::V1_0::PortStatus;
using ::android::hardware::usb::V1_0::PortDataRole;
using ::android::hardware::usb::V1_0::PortPowerRole;
using ::android::hardware::usb::V1_0::PortMode;
using ::android::hardware::usb::V1_0::Status;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace android {
namespace hardware {
namespace usb {
namespace V1_0 {
namespace implementation {

// Matches PortMode in types.hal. The gadget is device/sink only.
static constexpr PortMode kSupportedMode = PortMode::UFP;

struct Usb : public IUsb {
    Usb() {}
    virtual ~Usb() {}

    // Returns true when the legacy gadget is connected to a host
    // (state is CONFIGURED or CONNECTED).
    static bool gadgetConnected() {
        char buf[32];
        int fd = open("/sys/class/android_usb/android0/state", O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            ALOGE("cannot open gadget state node: %s", strerror(errno));
            return false;
        }
        int n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) {
            return false;
        }
        buf[n] = '\0';
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                buf[i] = '\0';
                break;
            }
        }
        return !strncmp(buf, "CONFIGURED", 10) || !strncmp(buf, "CONNECTED", 9);
    }

    void reportOnceLocked() {
        hidl_vec<PortStatus> currentPortStatus;
        currentPortStatus.resize(1);
        currentPortStatus[0].portName = "mocha";
        if (gadgetConnected()) {
            currentPortStatus[0].currentMode = PortMode::UFP;
            currentPortStatus[0].currentDataRole = PortDataRole::DEVICE;
            currentPortStatus[0].currentPowerRole = PortPowerRole::SINK;
        } else {
            // Mode 0 means "nothing connected": UsbPortStatus.isConnected()
            // checks currentMode != 0.
            currentPortStatus[0].currentMode = static_cast<PortMode>(0);
            currentPortStatus[0].currentDataRole = PortDataRole::NONE;
            currentPortStatus[0].currentPowerRole = PortPowerRole::NONE;
        }
        currentPortStatus[0].supportedModes = kSupportedMode;
        currentPortStatus[0].canChangeMode = false;
        currentPortStatus[0].canChangeDataRole = false;
        currentPortStatus[0].canChangePowerRole = false;

        auto status = Status::SUCCESS;
        mCallback->notifyPortStatusChange(currentPortStatus, status);
    }

    // IUsb
    Return<void> switchRole(const hidl_string& /*portName*/,
            const PortRole& role) override {
        // The port is not switchable; report the request as failed so the
        // framework does not wait for a state change that will never come.
        if (mCallback != nullptr) {
            mCallback->notifyRoleSwitchStatus("mocha", role, Status::ERROR);
        }
        return Void();
    }

    Return<void> setCallback(const sp<IUsbCallback>& callback) override {
        pthread_mutex_lock(&mLock);
        mCallback = callback;
        pthread_mutex_unlock(&mLock);
        return Void();
    }

    Return<void> queryPortStatus() override {
        pthread_mutex_lock(&mLock);
        if (mCallback != nullptr) {
            reportOnceLocked();
        }
        pthread_mutex_unlock(&mLock);
        return Void();
    }

    sp<IUsbCallback> mCallback;
    pthread_mutex_t mLock = PTHREAD_MUTEX_INITIALIZER;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace usb
}  // namespace hardware
}  // namespace android

int main() {
    using ::android::hardware::configureRpcThreadpool;
    using ::android::hardware::joinRpcThreadpool;
    using ::android::hardware::usb::V1_0::IUsb;
    using ::android::hardware::usb::V1_0::implementation::Usb;
    using ::android::sp;

    sp<IUsb> service = new Usb();

    configureRpcThreadpool(1, true /* callerWillJoin */);
    android::status_t status = service->registerAsService();

    if (status == android::OK) {
        ALOGI("USB HAL (mocha legacy gadget) Ready.");
        joinRpcThreadpool();
    }

    ALOGE("Cannot register USB HAL service");
    return 1;
}
