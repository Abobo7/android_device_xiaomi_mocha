/* Copyright (C) 2012 The CyanogenMod Project
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0 */
#define LOG_TAG "MochaCamera3"
#include "Camera3Wrapper.h"
#include "CameraWrapper.h"
#include "LegacyMetadata.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <errno.h>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <hardware/gralloc.h>
#include <log/log.h>

namespace {
using android::CameraMetadata;

// The stock callback supplies only this 16-byte prefix. Reading the Oreo
// input_buffer or partial_result members would read beyond the vendor object.
struct StockResult {
    uint32_t frame;
    const camera_metadata_t* metadata;
    uint32_t count;
    const camera3_stream_buffer_t* buffers;
};
static_assert(sizeof(StockResult) == 16, "mocha uses the 32-bit HAL3.0 result ABI");
static_assert(offsetof(camera3_capture_result_t, input_buffer) == sizeof(StockResult),
              "unexpected result ABI");

class Camera;
struct Callbacks {
    camera3_callback_ops_t ops;
    Camera* owner;
};

struct BufferReference {
    const gralloc_module_t* gralloc;
    buffer_handle_t handle;
    BufferReference(const gralloc_module_t* module, buffer_handle_t buffer)
        : gralloc(module), handle(buffer) {}
    ~BufferReference() { gralloc->unregisterBuffer(gralloc, handle); }
};

struct Stream {
    camera3_stream_t legacy = {};
    camera3_stream_t* client = nullptr;
    std::map<buffer_handle_t, std::unique_ptr<BufferReference>> buffers;
};

struct Frame {
    CameraMetadata settings;
    CameraMetadata partials;
    std::vector<camera3_stream_buffer_t> clientBuffers;
    std::vector<camera3_stream_buffer_t> vendorBuffers;
    std::vector<bool> returned;
    size_t remaining = 0;
    bool metadataDone = false;
    bool shutterDone = false;
};

class Camera {
public:
    camera3_device_t device = {};
    camera3_device_t* vendor = nullptr;
    const camera_metadata_t* characteristics = nullptr;
    const gralloc_module_t* gralloc = nullptr;

    Camera() {
        callbacks.ops.process_capture_result = resultCallback;
        callbacks.ops.notify = notifyCallback;
        callbacks.owner = this;
        device.priv = this;
    }

    int initialize(const camera3_callback_ops_t* client) {
        if (!client || !client->process_capture_result || !client->notify) return -EINVAL;
        std::lock_guard<std::mutex> requestLock(requestMutex);
        if (clientCallbacks) return -ENOSYS;
        clientCallbacks = client;
        int result = vendor->ops->initialize(vendor, &callbacks.ops);
        if (result) clientCallbacks = nullptr;
        return result;
    }

    int configure(camera3_stream_configuration_t* config) {
        if (!config || !config->num_streams || !config->streams ||
                config->operation_mode != CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE) return -EINVAL;
        std::lock_guard<std::mutex> requestLock(requestMutex);
        {
            std::lock_guard<std::mutex> stateLock(stateMutex);
            if (fatal || closing || !clientCallbacks) return -ENODEV;
            // HIDL holds its inflight lock during configure_streams. Never wait
            // for callbacks under that call; the framework must drain first.
            if (!frames.empty()) return -EBUSY;
        }
        std::map<camera3_stream_t*, std::unique_ptr<Stream>> next;
        std::vector<camera3_stream_t*> vendorStreams;
        unsigned processed = 0, jpeg = 0;
        for (unsigned i = 0; i < config->num_streams; ++i) {
            camera3_stream_t* client = config->streams[i];
            if (!client || client->stream_type != CAMERA3_STREAM_OUTPUT ||
                    !client->width || !client->height || next.count(client)) return -EINVAL;
            if (client->format == HAL_PIXEL_FORMAT_BLOB) ++jpeg;
            else if (client->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED ||
                     client->format == HAL_PIXEL_FORMAT_YCbCr_420_888 ||
                     client->format == HAL_PIXEL_FORMAT_RGBA_8888 ||
                     (client->format == HAL_PIXEL_FORMAT_YV12 && streams.count(client))) ++processed;
            else return -EINVAL;
            if (processed > 3 || jpeg > 1) return -EINVAL;
            std::unique_ptr<Stream> stream(new (std::nothrow) Stream);
            if (!stream) return -ENOMEM;
            stream->client = client;
            stream->legacy = *client;
            stream->legacy.priv = nullptr;
            vendorStreams.push_back(&stream->legacy);
            next.emplace(client, std::move(stream));
        }
        camera3_stream_configuration_t legacyConfig = {};
        legacyConfig.num_streams = vendorStreams.size();
        legacyConfig.streams = vendorStreams.data();
        int result = vendor->ops->configure_streams(vendor, &legacyConfig);
        if (result) {
            ALOGE("Stock configure_streams failed: %d", result);
            // Retain addresses until close even if the old HAL partially
            // adopted a rejected configuration.
            for (auto& item : next) failedStreams.push_back(std::move(item.second));
            std::lock_guard<std::mutex> stateLock(stateMutex);
            fatal = true;
            return result;
        }
        for (auto& item : next) {
            Stream& stream = *item.second;
            ALOGI("Stream %ux%u format 0x%x -> 0x%x, usage 0x%x, max buffers %u",
                  stream.client->width, stream.client->height, stream.client->format,
                  stream.legacy.format, stream.legacy.usage, stream.legacy.max_buffers);
            // The stock HAL resolves IMPLEMENTATION_DEFINED to YV12. HIDL
            // returns this as overrideFormat; the allocator cannot allocate the
            // unresolved 0x22 format. Subsequent configurations use that YV12.
            stream.client->format = stream.legacy.format;
            stream.client->usage = stream.legacy.usage;
            stream.client->max_buffers = stream.legacy.max_buffers;
            stream.client->priv = nullptr;
        }
        // configure has unlinked the old streams. Only now release the pinned
        // handles and stream objects that stock may have kept pointers to.
        {
            std::lock_guard<std::mutex> stateLock(stateMutex);
            streams.swap(next);
        }
        lastSettings.clear();
        return 0;
    }

    const camera_metadata_t* defaultRequest(int type) {
        if (type < CAMERA3_TEMPLATE_PREVIEW || type >= CAMERA3_TEMPLATE_COUNT) return nullptr;
        std::lock_guard<std::mutex> requestLock(requestMutex);
        CameraMetadata& cached = templates[type];
        if (cached.isEmpty()) {
            const camera_metadata_t* stock = vendor->ops->construct_default_request_settings(vendor, type);
            if (!stock) return nullptr;
            camera_metadata_t* copy = clone_camera_metadata(stock);
            if (!copy) return nullptr;
            cached.acquire(copy);
            if (!mocha::normalizeRequest(&cached, characteristics)) {
                cached.clear();
                return nullptr;
            }
        }
        const camera_metadata_t* result = cached.getAndLock();
        cached.unlock(result);
        return result;
    }

    int capture(camera3_capture_request_t* request) {
        if (!request || request->input_buffer || !request->num_output_buffers ||
                !request->output_buffers) return -EINVAL;
        std::lock_guard<std::mutex> requestLock(requestMutex);
        {
            std::lock_guard<std::mutex> stateLock(stateMutex);
            if (fatal || closing || !clientCallbacks) return -ENODEV;
            if (frames.count(request->frame_number)) return -EINVAL;
        }
        bool newBuffer = false;
        std::vector<Stream*> requestedStreams;
        for (unsigned i = 0; i < request->num_output_buffers; ++i) {
            const auto& buffer = request->output_buffers[i];
            auto found = streams.find(buffer.stream);
            if (found == streams.end() || !buffer.buffer || !*buffer.buffer ||
                    std::find(requestedStreams.begin(), requestedStreams.end(), found->second.get()) !=
                        requestedStreams.end()) return -EINVAL;
            requestedStreams.push_back(found->second.get());
            if (!found->second->buffers.count(*buffer.buffer)) newBuffer = true;
        }
        if (newBuffer) {
            // Stock re-registration first unlinks the previous entire set.
            // It is only safe when every earlier result/buffer has been delivered.
            int result = drain();
            if (result) return result;
            for (unsigned i = 0; i < request->num_output_buffers; ++i) {
                Stream& stream = *requestedStreams[i];
                buffer_handle_t handle = *request->output_buffers[i].buffer;
                if (stream.buffers.count(handle)) continue;
                int result = gralloc->registerBuffer(gralloc, handle);
                if (result) return result;
                std::unique_ptr<BufferReference> ref(new (std::nothrow) BufferReference(gralloc, handle));
                if (!ref) { gralloc->unregisterBuffer(gralloc, handle); return -ENOMEM; }
                stream.buffers.emplace(handle, std::move(ref));
                std::vector<buffer_handle_t*> handles;
                for (auto& item : stream.buffers) handles.push_back(&item.second->handle);
                camera3_stream_buffer_set_t set = {&stream.legacy,
                    static_cast<uint32_t>(handles.size()), handles.data()};
                result = vendor->ops->register_stream_buffers(vendor, &set);
                if (result) {
                    ALOGE("Stock register_stream_buffers failed: %d", result);
                    std::lock_guard<std::mutex> stateLock(stateMutex);
                    fatal = true;
                    return result;
                }
            }
        }
        std::shared_ptr<Frame> frame = std::make_shared<Frame>();
        if (request->settings) {
            frame->settings = request->settings;
            if (frame->settings.isEmpty() || !mocha::normalizeRequest(&frame->settings, characteristics))
                return -EINVAL;
            // HAL3.0 identifies trigger events by these deprecated ids. HAL3.2
            // clients can send START/CANCEL without changing the old id fields.
            auto af = frame->settings.find(ANDROID_CONTROL_AF_TRIGGER);
            if (af.count && af.data.u8[0] != ANDROID_CONTROL_AF_TRIGGER_IDLE)
                afTriggerId = afTriggerId == INT32_MAX ? 1 : afTriggerId + 1;
            auto ae = frame->settings.find(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER);
            if (ae.count && ae.data.u8[0] == ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_START)
                aeTriggerId = aeTriggerId == INT32_MAX ? 1 : aeTriggerId + 1;
            if (frame->settings.update(ANDROID_CONTROL_AF_TRIGGER_ID, &afTriggerId, 1) ||
                    frame->settings.update(ANDROID_CONTROL_AE_PRECAPTURE_ID, &aeTriggerId, 1)) return -ENOMEM;
            lastSettings = frame->settings;
        } else if (lastSettings.isEmpty()) return -EINVAL;
        frame->remaining = request->num_output_buffers;
        frame->clientBuffers.assign(request->output_buffers,
                                    request->output_buffers + request->num_output_buffers);
        frame->vendorBuffers = frame->clientBuffers;
        frame->returned.resize(request->num_output_buffers, false);
        for (unsigned i = 0; i < request->num_output_buffers; ++i) {
            Stream& stream = *requestedStreams[i];
            frame->vendorBuffers[i].stream = &stream.legacy;
            frame->vendorBuffers[i].buffer = &stream.buffers.at(*request->output_buffers[i].buffer)->handle;
        }
        camera3_capture_request_t legacyRequest = *request;
        legacyRequest.output_buffers = frame->vendorBuffers.data();
        legacyRequest.settings = request->settings ? frame->settings.getAndLock() : nullptr;
        {
            std::lock_guard<std::mutex> stateLock(stateMutex);
            frames.emplace(request->frame_number, frame);
        }
        int result = vendor->ops->process_capture_request(vendor, &legacyRequest);
        if (legacyRequest.settings) frame->settings.unlock(legacyRequest.settings);
        if (result) {
            std::lock_guard<std::mutex> stateLock(stateMutex);
            frames.erase(request->frame_number);
            drained.notify_all();
        }
        return result;
    }

    int flush() {
        std::lock_guard<std::mutex> requestLock(requestMutex);
        // HAL3.0 has no flush entry. With requests serialized, draining returns
        // all accepted buffers and metadata without calling a reserved ops slot.
        return drain();
    }

    int close() {
        std::lock_guard<std::mutex> requestLock(requestMutex);
        {
            std::lock_guard<std::mutex> stateLock(stateMutex);
            closing = true;
        }
        int result = vendor->common.close(&vendor->common);
        vendor = nullptr;
        frames.clear();
        streams.clear();
        failedStreams.clear();
        return result;
    }

    void dump(int fd) {
        {
            std::lock_guard<std::mutex> stateLock(stateMutex);
            dprintf(fd, "Mocha HAL3.0 adapter: inflight=%zu fatal=%d closing=%d\n",
                    frames.size(), fatal, closing);
        }
        if (vendor->ops->dump) vendor->ops->dump(vendor, fd);
    }

private:
    Callbacks callbacks = {};
    const camera3_callback_ops_t* clientCallbacks = nullptr;
    std::mutex requestMutex, stateMutex, callbackMutex;
    std::condition_variable drained;
    std::map<camera3_stream_t*, std::unique_ptr<Stream>> streams;
    std::vector<std::unique_ptr<Stream>> failedStreams;
    std::map<uint32_t, std::shared_ptr<Frame>> frames;
    std::array<CameraMetadata, CAMERA3_TEMPLATE_COUNT> templates;
    CameraMetadata lastSettings;
    int32_t afTriggerId = 0, aeTriggerId = 0;
    bool fatal = false;
    bool closing = false;

    int drain() {
        std::unique_lock<std::mutex> lock(stateMutex);
        bool ready = drained.wait_for(lock, std::chrono::seconds(3),
                                      [this] { return frames.empty() || fatal; });
        if (!ready) {
            ALOGE("Timed out draining %zu stock capture requests", frames.size());
            return -ETIMEDOUT;
        }
        return fatal ? -ENODEV : 0;
    }

    void retire(uint32_t number) {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto found = frames.find(number);
        if (found != frames.end() && !found->second->remaining &&
                found->second->metadataDone && found->second->shutterDone) {
            frames.erase(found);
            drained.notify_all();
        }
    }

    static void resultCallback(const camera3_callback_ops_t* ops, const camera3_capture_result_t* raw) {
        reinterpret_cast<const Callbacks*>(ops)->owner->result(reinterpret_cast<const StockResult*>(raw));
    }

    void result(const StockResult* stock) {
        std::lock_guard<std::mutex> callbackLock(callbackMutex);
        CameraMetadata metadata;
        std::vector<camera3_stream_buffer_t> output;
        bool metadataError = false;
        bool hasMetadata = false;
        {
            std::lock_guard<std::mutex> stateLock(stateMutex);
            auto found = frames.find(stock->frame);
            if (found == frames.end()) {
                ALOGE("Unexpected stock result frame %u", stock->frame);
                for (unsigned i = 0; i < stock->count; ++i)
                    if (stock->buffers[i].release_fence >= 0) ::close(stock->buffers[i].release_fence);
                return;
            }
            Frame& frame = *found->second;
            if (stock->metadata && !frame.metadataDone) {
                camera_metadata_ro_entry_t marker = {};
                bool partial = !find_camera_metadata_ro_entry(stock->metadata,
                        ANDROID_QUIRKS_PARTIAL_RESULT, &marker) && marker.count == 1 &&
                        marker.data.u8[0] == ANDROID_QUIRKS_PARTIAL_RESULT_PARTIAL;
                if (partial) {
                    metadataError = !mocha::mergeMetadata(&frame.partials, stock->metadata);
                } else {
                    hasMetadata = mocha::mergeMetadata(&metadata, stock->metadata) &&
                                  mocha::finishResult(&metadata, frame.partials);
                    metadataError = !hasMetadata;
                    frame.metadataDone = true;
                }
                if (metadataError) frame.metadataDone = true;
            }
            for (unsigned i = 0; i < stock->count; ++i) {
                const camera3_stream_buffer_t& returned = stock->buffers[i];
                size_t n;
                for (n = 0; n < frame.vendorBuffers.size(); ++n) {
                    const auto& expected = frame.vendorBuffers[n];
                    if (returned.stream == expected.stream && returned.buffer &&
                            *returned.buffer == *expected.buffer && !frame.returned[n]) break;
                }
                if (n == frame.vendorBuffers.size()) {
                    ALOGE("Unexpected stock buffer for frame %u", stock->frame);
                    if (returned.release_fence >= 0) ::close(returned.release_fence);
                    fatal = true;
                    drained.notify_all();
                    continue;
                }
                auto converted = frame.clientBuffers[n];
                converted.status = returned.status;
                converted.acquire_fence = -1;
                converted.release_fence = returned.release_fence;
                output.push_back(converted);
                frame.returned[n] = true;
                --frame.remaining;
            }
        }
        if (metadataError) {
            camera3_notify_msg_t error = {};
            error.type = CAMERA3_MSG_ERROR;
            error.message.error.frame_number = stock->frame;
            error.message.error.error_code = CAMERA3_MSG_ERROR_RESULT;
            clientCallbacks->notify(clientCallbacks, &error);
        }
        if (hasMetadata || !output.empty()) {
            camera3_capture_result_t converted = {};
            converted.frame_number = stock->frame;
            converted.result = hasMetadata ? metadata.getAndLock() : nullptr;
            converted.num_output_buffers = output.size();
            converted.output_buffers = output.empty() ? nullptr : output.data();
            converted.input_buffer = nullptr;
            converted.partial_result = hasMetadata ? 1 : 0;
            clientCallbacks->process_capture_result(clientCallbacks, &converted);
            if (converted.result) metadata.unlock(converted.result);
        }
        // Do not unblock re-registration until the client has consumed this
        // callback, including its buffer handles and metadata.
        retire(stock->frame);
    }

    static void notifyCallback(const camera3_callback_ops_t* ops, const camera3_notify_msg_t* message) {
        reinterpret_cast<const Callbacks*>(ops)->owner->notify(message);
    }

    void notify(const camera3_notify_msg_t* message) {
        std::lock_guard<std::mutex> callbackLock(callbackMutex);
        camera3_notify_msg_t converted = *message;
        uint32_t number = 0;
        bool hasFrame = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (message->type == CAMERA3_MSG_SHUTTER) {
                number = message->message.shutter.frame_number;
                hasFrame = true;
                auto found = frames.find(number);
                if (found != frames.end()) found->second->shutterDone = true;
            } else if (message->type == CAMERA3_MSG_ERROR) {
                number = message->message.error.frame_number;
                hasFrame = true;
                int code = message->message.error.error_code;
                auto found = frames.find(number);
                if (found != frames.end()) {
                    if (code == CAMERA3_MSG_ERROR_REQUEST || code == CAMERA3_MSG_ERROR_RESULT)
                        found->second->metadataDone = true;
                    if (code == CAMERA3_MSG_ERROR_REQUEST) found->second->shutterDone = true;
                }
                converted.message.error.error_stream = nullptr;
                for (auto& item : streams) {
                    if (&item.second->legacy == message->message.error.error_stream)
                        converted.message.error.error_stream = item.second->client;
                }
                if (code == CAMERA3_MSG_ERROR_DEVICE) { fatal = true; drained.notify_all(); }
            }
        }
        clientCallbacks->notify(clientCallbacks, &converted);
        if (hasFrame) retire(number);
    }
};

Camera* camera(const camera3_device_t* device) { return device ? static_cast<Camera*>(device->priv) : nullptr; }
int initialize(const camera3_device_t* device, const camera3_callback_ops_t* ops) {
    return camera(device) ? camera(device)->initialize(ops) : -EINVAL;
}
int configure(const camera3_device_t* device, camera3_stream_configuration_t* streams) {
    return camera(device) ? camera(device)->configure(streams) : -EINVAL;
}
const camera_metadata_t* defaultRequest(const camera3_device_t* device, int type) {
    return camera(device) ? camera(device)->defaultRequest(type) : nullptr;
}
int capture(const camera3_device_t* device, camera3_capture_request_t* request) {
    return camera(device) ? camera(device)->capture(request) : -EINVAL;
}
int flush(const camera3_device_t* device) { return camera(device) ? camera(device)->flush() : -EINVAL; }
void dump(const camera3_device_t* device, int fd) { if (camera(device)) camera(device)->dump(fd); }
int closeDevice(hw_device_t* device) {
    if (!device) return -EINVAL;
    Camera* adapter = camera(reinterpret_cast<camera3_device_t*>(device));
    int result = adapter->close();
    delete adapter;
    return result;
}
camera3_device_ops_t operations = {
    .initialize = initialize,
    .configure_streams = configure,
    .register_stream_buffers = nullptr,
    .construct_default_request_settings = defaultRequest,
    .process_capture_request = capture,
    .get_metadata_vendor_tag_ops = nullptr,
    .dump = dump,
    .flush = flush,
};
}  // namespace

int camera3_device_open(const hw_module_t* module, const char* name, hw_device_t** device) {
    if (!module || !name || !*name || !device) return -EINVAL;
    *device = nullptr;
    // Reject negative ids, trailing junk and the old off-by-one count boundary.
    for (const char* c = name; *c; ++c) if (*c < '0' || *c > '9') return -EINVAL;
    errno = 0;
    char* end = nullptr;
    long id = strtol(name, &end, 10);
    if (errno || *end || id < 0 || id >= 2) return -EINVAL;
    camera_info info = {};
    int result = get_mocha_camera_info(id, &info);
    if (result) return result;
    camera_module_t* stock = get_stock_camera_module();
    if (!stock || id >= stock->get_number_of_cameras()) return -EINVAL;
    std::unique_ptr<Camera> adapter(new (std::nothrow) Camera);
    if (!adapter) return -ENOMEM;
    const hw_module_t* gralloc = nullptr;
    result = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &gralloc);
    if (result) return result;
    adapter->gralloc = reinterpret_cast<const gralloc_module_t*>(gralloc);
    if (!adapter->gralloc->registerBuffer || !adapter->gralloc->unregisterBuffer) return -ENODEV;
    hw_device_t* opened = nullptr;
    result = stock->common.methods->open(&stock->common, name, &opened);
    if (result) return result;
    if (!opened) return -ENODEV;
    adapter->vendor = reinterpret_cast<camera3_device_t*>(opened);
    if (opened->version != CAMERA_DEVICE_API_VERSION_3_0 || !adapter->vendor->ops ||
            !adapter->vendor->ops->initialize || !adapter->vendor->ops->configure_streams ||
            !adapter->vendor->ops->register_stream_buffers ||
            !adapter->vendor->ops->construct_default_request_settings ||
            !adapter->vendor->ops->process_capture_request) {
        opened->close(opened);
        return -ENODEV;
    }
    adapter->characteristics = info.static_camera_characteristics;
    adapter->device.common.tag = HARDWARE_DEVICE_TAG;
    adapter->device.common.version = CAMERA_DEVICE_API_VERSION_3_2;
    adapter->device.common.module = const_cast<hw_module_t*>(module);
    adapter->device.common.close = closeDevice;
    adapter->device.ops = &operations;
    *device = &adapter.release()->device.common;
    return 0;
}
