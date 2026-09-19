// Actual Camera3Wrapper.cpp, fake HAL/gralloc, real C metadata allocator.
// Built as 32-bit x86 so the production HAL3.0 ABI assertions remain enabled.
#undef NDEBUG
#include <assert.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include "../camera/Camera3Wrapper.cpp"

struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool enabled = false, entered = false, released = false;
    void arm() {
        std::lock_guard<std::mutex> lock(mutex);
        enabled = true;
        entered = released = false;
    }
    void visit() {
        std::unique_lock<std::mutex> lock(mutex);
        if (!enabled) return;
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return released; });
        enabled = false;
    }
    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        assert(cv.wait_for(lock, std::chrono::seconds(3), [&] { return entered; }));
    }
    void release() {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        cv.notify_all();
    }
};

static Gate configure_gate, close_gate, dump_gate;
static std::atomic<int> opens{0}, closes{0}, dumps{0};
static std::atomic<bool> fail_open{false};
static camera_module_t stock_module = {};
static gralloc_module_t fake_gralloc = {};
struct FakeVendor {
    camera3_device_t device = {};
    camera_metadata_t* request = nullptr;
    int generation;
};
static FakeVendor* fake(const camera3_device_t* device) {
    return static_cast<FakeVendor*>(device->priv);
}
static int stock_initialize(const camera3_device_t*, const camera3_callback_ops_t*) { return 0; }
static int stock_configure(const camera3_device_t*, camera3_stream_configuration_t*) {
    configure_gate.visit();
    return 0;
}
static int stock_register(const camera3_device_t*, const camera3_stream_buffer_set_t*) {
    abort();  // No capture/buffer registration is in this test's scope.
}
static const camera_metadata_t* stock_default(const camera3_device_t* device, int) {
    return fake(device)->request;
}
static int stock_capture(const camera3_device_t*, camera3_capture_request_t*) { abort(); }
static int stock_close(hw_device_t* device) {
    close_gate.visit();
    FakeVendor* vendor = fake(reinterpret_cast<camera3_device_t*>(device));
    free_camera_metadata(vendor->request);
    delete vendor;
    ++closes;
    return 0;
}
static void stock_dump(const camera3_device_t* device, int fd) {
    ++dumps;
    dump_gate.visit();
    // A concurrent unprotected close would make this an ASan use-after-free.
    dprintf(fd, "fake stock generation=%d\n", fake(device)->generation);
}
static camera3_device_ops_t stock_ops = {
    .initialize = stock_initialize,
    .configure_streams = stock_configure,
    .register_stream_buffers = stock_register,
    .construct_default_request_settings = stock_default,
    .process_capture_request = stock_capture,
    .get_metadata_vendor_tag_ops = nullptr,
    .dump = stock_dump,
};
static int stock_open(const hw_module_t*, const char*, hw_device_t** out) {
    *out = nullptr;
    if (fail_open.exchange(false)) return -ENODEV;
    FakeVendor* vendor = new FakeVendor;
    vendor->generation = ++opens;
    vendor->device.common.version = CAMERA_DEVICE_API_VERSION_3_0;
    vendor->device.common.close = stock_close;
    vendor->device.ops = &stock_ops;
    vendor->device.priv = vendor;
    vendor->request = allocate_camera_metadata(1, 0);
    assert(vendor->request);
    int32_t generation = vendor->generation;
    assert(add_camera_metadata_entry(vendor->request, ANDROID_REQUEST_ID, &generation, 1) == 0);
    *out = &vendor->device.common;
    return 0;
}
static hw_module_methods_t stock_methods = { .open = stock_open };
static int stock_count() { return 2; }
static int gralloc_register(const gralloc_module_t*, buffer_handle_t) { abort(); }
static int gralloc_unregister(const gralloc_module_t*, buffer_handle_t) { abort(); }
extern "C" int hw_get_module(const char* id, const hw_module_t** module) {
    assert(strcmp(id, GRALLOC_HARDWARE_MODULE_ID) == 0);
    *module = &fake_gralloc.common;
    return 0;
}
camera_module_t* get_stock_camera_module() { return &stock_module; }
int get_mocha_camera_info(int id, camera_info* info) {
    assert(id >= 0 && id < 2);
    *info = {};
    return 0;
}
namespace mocha {
bool normalizeRequest(CameraMetadata* request, const camera_metadata_t*) {
    return !request->isEmpty();  // Normalization is deliberately not modeled.
}
bool mergeMetadata(CameraMetadata*, const camera_metadata_t*) { abort(); }
bool finishResult(CameraMetadata*, const CameraMetadata&) { abort(); }
}
static void client_result(const camera3_callback_ops_t*, const camera3_capture_result_t*) { abort(); }
static void client_notify(const camera3_callback_ops_t*, const camera3_notify_msg_t*) { abort(); }
static camera3_callback_ops_t client_callbacks = {client_result, client_notify};

static camera3_device_t* open_camera() {
    hw_device_t* device = nullptr;
    assert(camera3_device_open(&stock_module.common, "0", &device) == 0);
    camera3_device_t* camera = reinterpret_cast<camera3_device_t*>(device);
    assert(camera->common.version == CAMERA_DEVICE_API_VERSION_3_2);
    assert(camera->ops->initialize(camera, &client_callbacks) == 0);
    return camera;
}
static camera3_stream_t stream(unsigned width, unsigned height, int usage = 0) {
    camera3_stream_t result = {};
    result.stream_type = CAMERA3_STREAM_OUTPUT;
    result.width = width;
    result.height = height;
    result.format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
    result.usage = usage;
    return result;
}
static int set_streams(camera3_device_t* camera, std::initializer_list<camera3_stream_t*> streams) {
    std::vector<camera3_stream_t*> list(streams);
    camera3_stream_configuration_t config = {};
    config.num_streams = list.size();
    config.streams = list.data();
    return camera->ops->configure_streams(camera, &config);
}
static void check_template(const camera_metadata_t* request, int generation) {
    assert(request && get_camera_metadata_entry_count(request) == 1);
    camera_metadata_ro_entry_t entry = {};
    assert(find_camera_metadata_ro_entry(request, ANDROID_REQUEST_ID, &entry) == 0);
    assert(entry.count == 1 && entry.data.i32[0] == generation);
}
static std::string dump_text(camera3_device_t* camera) {
    int fds[2];
    assert(pipe(fds) == 0);
    camera->ops->dump(camera, fds[1]);
    ::close(fds[1]);
    char text[512] = {};
    ssize_t count = read(fds[0], text, sizeof(text));
    assert(count > 0);
    ::close(fds[0]);
    return std::string(text, count);
}
static void check_busy_dump(camera3_device_t* camera) {
    int before = dumps;
    auto result = std::async(std::launch::async, [&] { return dump_text(camera); });
    assert(result.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready);
    assert(result.get().find("busy; stock dump skipped") != std::string::npos);
    assert(dumps == before);
}
static void test_lifetime_and_dump() {
    camera3_device_t* camera = open_camera();
    int first_generation = opens;
    const auto* preview_request =
            camera->ops->construct_default_request_settings(camera, CAMERA3_TEMPLATE_PREVIEW);
    check_template(preview_request, first_generation);
    auto preview = stream(1920, 1080);
    auto callback = stream(1920, 1080);
    auto encoder = stream(3840, 2160, GRALLOC_USAGE_HW_VIDEO_ENCODER);
    assert(set_streams(camera, {&preview}) == 0);
    assert(set_streams(camera, {&preview, &callback, &encoder}) == 0);
    assert(set_streams(camera, {&preview}) == 0);
    assert(opens == first_generation);  // Keep the existing geometry fix.
    auto smaller = stream(1280, 720);
    assert(set_streams(camera, {&smaller}) == 0);
    assert(opens == first_generation + 1);
    check_template(preview_request, first_generation);
    assert(camera->ops->construct_default_request_settings(camera, CAMERA3_TEMPLATE_PREVIEW) ==
            preview_request);
    const auto* video_request =
            camera->ops->construct_default_request_settings(camera, CAMERA3_TEMPLATE_VIDEO_RECORD);
    check_template(video_request, first_generation + 1);
    puts("PASS: stable template pointers across internal reopen; encoder/duplicate geometry unchanged");

    assert(dump_text(camera).find("fake stock generation=") != std::string::npos);
    configure_gate.arm();
    auto configuring = std::async(std::launch::async, [&] { return set_streams(camera, {&smaller}); });
    configure_gate.wait();
    check_busy_dump(camera);
    configure_gate.release();
    assert(configuring.get() == 0);

    // An active stock dump pins the vendor until it returns.
    dump_gate.arm();
    auto dumping = std::async(std::launch::async, [&] { return dump_text(camera); });
    dump_gate.wait();
    int before_close = closes;
    std::promise<void> started;
    auto reconfiguring = std::async(std::launch::async, [&] {
        started.set_value();
        return set_streams(camera, {&preview});
    });
    started.get_future().wait();
    assert(reconfiguring.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
    assert(closes == before_close);
    dump_gate.release();
    assert(dumping.get().find("fake stock generation=") != std::string::npos);
    assert(reconfiguring.get() == 0);
    check_template(preview_request, first_generation);
    check_template(video_request, first_generation + 1);

    close_gate.arm();
    auto closing = std::async(std::launch::async, [&] {
        return camera->common.close(&camera->common);
    });
    close_gate.wait();
    check_busy_dump(camera);
    close_gate.release();
    assert(closing.get() == 0);
    puts("PASS: dump skips busy configure/close without waiting; active dump pins vendor lifetime");
}
static void test_failed_reopen() {
    camera3_device_t* camera = open_camera();
    int generation = opens;
    const auto* request =
            camera->ops->construct_default_request_settings(camera, CAMERA3_TEMPLATE_PREVIEW);
    auto preview = stream(1920, 1080);
    auto smaller = stream(1280, 720);
    assert(set_streams(camera, {&preview}) == 0);
    fail_open = true;
    assert(set_streams(camera, {&smaller}) == -ENODEV);
    check_template(request, generation);
    assert(camera->ops->construct_default_request_settings(camera, CAMERA3_TEMPLATE_PREVIEW) == nullptr);
    assert(dump_text(camera).find("fatal=1") != std::string::npos);
    assert(camera->common.close(&camera->common) == 0);
    puts("PASS: failed vendor reopen retains previously returned templates and closes safely");
}
int main() {
    alarm(20);
    stock_module.common.methods = &stock_methods;
    stock_module.get_number_of_cameras = stock_count;
    fake_gralloc.registerBuffer = gralloc_register;
    fake_gralloc.unregisterBuffer = gralloc_unregister;
    test_lifetime_and_dump();
    test_failed_reopen();
    assert(opens == closes);
    return 0;
}
