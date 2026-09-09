/* Copyright (C) 2015 The CyanogenMod Project
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0 */
#define LOG_TAG "MochaCameraModule"
#include "CameraWrapper.h"
#include "Camera3Wrapper.h"
#include "LegacyMetadata.h"

#include <array>
#include <dlfcn.h>
#include <errno.h>
#include <mutex>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <log/log.h>

namespace {
camera_module_t* vendorModule = nullptr;
pthread_once_t vendorOnce = PTHREAD_ONCE_INIT;
std::mutex infoMutex;
std::array<android::CameraMetadata, 2> characteristics;

bool prepareImagerDevices() {
    // Tegra's discovery ioctl creates the I2C devices dynamically. The stock
    // camera module immediately opens them and caches a shortened camera list
    // if ueventd has not yet created the character nodes. Run the stock
    // discovery first, then wait for the same service UID to access both nodes.
    // Keep this reference alive: the discovery state belongs to this library.
    static void* imager = dlopen("libnvodm_imager.so", RTLD_NOW | RTLD_LOCAL);
    if (!imager) {
        ALOGE("Cannot load stock imager discovery: %s", dlerror());
        return false;
    }
    using DetectDevices = int (*)();
    // DetInit creates the library's discovery mutex and is idempotent. Calling
    // DeviceDetect before DetInit returns false without probing any hardware.
    auto initialize = reinterpret_cast<DetectDevices>(dlsym(imager, "NvOdmImagerDetInit"));
    auto detect = reinterpret_cast<DetectDevices>(dlsym(imager, "NvOdmImagerDeviceDetect"));
    if (!initialize || !detect || !initialize() || !detect()) {
        ALOGE("Stock imager discovery failed");
        return false;
    }
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (access("/dev/imx179", R_OK | W_OK) == 0 &&
                access("/dev/ov5693", R_OK | W_OK) == 0) {
            ALOGI("Both imager nodes ready after %d ms", attempt * 20);
            return true;
        }
        usleep(20000);
    }
    ALOGE("Timed out waiting for imx179/ov5693 device nodes");
    return false;
}

void loadVendorModule() {
    if (!prepareImagerDevices()) return;
    const hw_module_t* raw = nullptr;
    int result = hw_get_module_by_class(CAMERA_HARDWARE_MODULE_ID, "vendor", &raw);
    if (result || !raw) {
        ALOGE("Cannot load stock camera module: %d", result);
        return;
    }
    camera_module_t* module = reinterpret_cast<camera_module_t*>(const_cast<hw_module_t*>(raw));
    if (module->common.module_api_version < CAMERA_MODULE_API_VERSION_2_1 ||
            !module->get_number_of_cameras || !module->get_camera_info ||
            !module->common.methods || !module->common.methods->open) {
        ALOGE("Unsupported stock camera module API 0x%x", module->common.module_api_version);
        return;
    }
    vendorModule = module;
}

int numberOfCameras() {
    camera_module_t* vendor = get_stock_camera_module();
    return vendor ? vendor->get_number_of_cameras() : 0;
}

int setCallbacks(const camera_module_callbacks_t* callbacks) {
    camera_module_t* vendor = get_stock_camera_module();
    if (!vendor) return -ENODEV;
    return vendor->set_callbacks ? vendor->set_callbacks(callbacks) : 0;
}

void vendorTags(vendor_tag_ops_t* ops) {
    if (!ops) return;
    memset(ops, 0, sizeof(*ops));
    camera_module_t* vendor = get_stock_camera_module();
    // The 2.1 stock metadata has no vendor tags. Do not read a later ABI's
    // optional method slot on a module which does not advertise that ABI.
    if (vendor && vendor->common.module_api_version >= CAMERA_MODULE_API_VERSION_2_2 &&
            vendor->get_vendor_tag_ops) vendor->get_vendor_tag_ops(ops);
}

int openLegacy(const hw_module_t*, const char*, uint32_t, hw_device_t**) {
    return -ENOSYS;
}
hw_module_methods_t moduleMethods = {.open = camera3_device_open};
}  // namespace

camera_module_t* get_stock_camera_module() {
    pthread_once(&vendorOnce, loadVendorModule);
    return vendorModule;
}

int get_mocha_camera_info(int id, camera_info* info) {
    if (!info || id < 0 || static_cast<size_t>(id) >= characteristics.size()) return -EINVAL;
    camera_module_t* vendor = get_stock_camera_module();
    if (!vendor) return -ENODEV;
    if (id >= vendor->get_number_of_cameras()) return -EINVAL;
    std::lock_guard<std::mutex> lock(infoMutex);
    camera_info stock = {};
    int result = vendor->get_camera_info(id, &stock);
    if (result) return result;
    if (stock.device_version != CAMERA_DEVICE_API_VERSION_3_0) return -ENODEV;
    if (characteristics[id].isEmpty() &&
            !mocha::buildCharacteristics(stock.static_camera_characteristics, &characteristics[id])) {
        ALOGE("Camera %d has invalid stock characteristics", id);
        return -EINVAL;
    }
    memset(info, 0, sizeof(*info));
    info->facing = stock.facing;
    info->orientation = stock.orientation;
    info->device_version = CAMERA_DEVICE_API_VERSION_3_2;
    const camera_metadata_t* raw = characteristics[id].getAndLock();
    info->static_camera_characteristics = raw;
    characteristics[id].unlock(raw);
    return 0;
}

camera_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = CAMERA_MODULE_API_VERSION_2_3,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = CAMERA_HARDWARE_MODULE_ID,
        .name = "Mocha stock HAL3.0 compatibility adapter",
        .author = "The LineageOS Project",
        .methods = &moduleMethods,
    },
    .get_number_of_cameras = numberOfCameras,
    .get_camera_info = get_mocha_camera_info,
    .set_callbacks = setCallbacks,
    .get_vendor_tag_ops = vendorTags,
    .open_legacy = openLegacy,
};
