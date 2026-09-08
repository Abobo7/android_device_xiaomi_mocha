/* Runtime ABI checks for the two constructors used by stock Tegra blobs. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <hardware/gralloc.h>
#include <new>
#include <vector>

#include <ui/GraphicBuffer.h>

using android::GraphicBuffer;
using android::sp;

extern "C" void legacyHandleCtor(void*, uint32_t, uint32_t, int32_t,
        uint32_t, uint32_t, native_handle_t*, bool)
    asm("_ZN7android13GraphicBufferC1EjjijjP13native_handleb");
extern "C" void legacyWindowCtor(void*, ANativeWindowBuffer*, bool)
    asm("_ZN7android13GraphicBufferC1EP19ANativeWindowBufferb");

#define CHECK(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); std::exit(1); \
} } while (0)

static constexpr size_t kLegacySize = 120;
static constexpr size_t kGuardSize = 32;
static int wrappedReferences = 1;

static void nativeIncRef(android_native_base_t*) { ++wrappedReferences; }
static void nativeDecRef(android_native_base_t*) { --wrappedReferences; }

static void* allocateLegacyObject()
{
    void* memory = ::operator new(kLegacySize + kGuardSize);
    std::memset(memory, 0xa5, kLegacySize + kGuardSize);
    return memory;
}

static void checkGuard(const void* memory)
{
    const auto* bytes = static_cast<const unsigned char*>(memory);
    for (size_t index = kLegacySize; index < kLegacySize + kGuardSize; ++index)
        CHECK(bytes[index] == 0xa5);
}

static void checkGrallocRetention()
{
    const hw_module_t* module = nullptr;
    CHECK(hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module) == 0);
    auto gralloc = reinterpret_cast<const gralloc_module_t*>(module);
    sp<GraphicBuffer> buffer = new GraphicBuffer(64, 64, 1,
            GraphicBuffer::USAGE_SW_READ_OFTEN | GraphicBuffer::USAGE_SW_WRITE_OFTEN);
    CHECK(buffer->initCheck() == 0 && buffer->handle != nullptr);
    buffer_handle_t retained = buffer->handle;
    std::vector<int> fds(retained->data, retained->data + retained->numFds);
    CHECK(!fds.empty());
    void* pixels = nullptr;
    CHECK(buffer->lock(GraphicBuffer::USAGE_SW_WRITE_OFTEN, &pixels) == 0);
    CHECK(pixels != nullptr);
    *static_cast<uint32_t*>(pixels) = 0xff1467abU;
    CHECK(buffer->unlock() == 0);

    // Match the stock HWC retaining this very handle across frame changes.
    CHECK(gralloc->registerBuffer(gralloc, retained) == 0);
    buffer.clear();
    // The old mapper closed these FDs and deleted the still-retained handle.
    // Detect that before attempting any access through a dangling pointer.
    for (int fd : fds)
        CHECK(fcntl(fd, F_GETFD) >= 0);
    CHECK(retained->version == sizeof(native_handle_t));
    pixels = nullptr;
    CHECK(gralloc->lock(gralloc, retained, GRALLOC_USAGE_SW_READ_OFTEN,
            0, 0, 64, 64, &pixels) == 0);
    CHECK(pixels != nullptr);
    CHECK(*static_cast<const uint32_t*>(pixels) == 0xff1467abU);
    CHECK(gralloc->unlock(gralloc, retained) == 0);
    CHECK(gralloc->unregisterBuffer(gralloc, retained) == 0);
    for (int fd : fds) {
        errno = 0;
        CHECK(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
    }
    std::puts("PASS HWC-retained gralloc handle, pixel readback and final FD cleanup");
}

int main()
{
    setbuf(stdout, nullptr);
    static_assert(sizeof(GraphicBuffer) == kLegacySize, "Legacy allocation must fit");
    void* storage = allocateLegacyObject();
    legacyHandleCtor(storage, 13, 7, 1, 0xf0000001U, 16, nullptr, false);
    auto* buffer = static_cast<GraphicBuffer*>(storage);
    buffer->incStrong(storage);
    checkGuard(storage);
    CHECK(buffer->initCheck() == 0);
    CHECK(buffer->getWidth() == 13 && buffer->getHeight() == 7);
    CHECK(buffer->getStride() == 16 && buffer->getLayerCount() == 1);
    CHECK(buffer->getUsage() == 0xf0000001ULL);
    CHECK(buffer->getId() != 0);
    buffer->setGenerationNumber(42);

    std::vector<unsigned char> flattened(buffer->getFlattenedSize());
    void* writeBuffer = flattened.data();
    size_t remaining = flattened.size();
    int fdStorage[1];
    int* writeFds = fdStorage;
    size_t fdCount = 0;
    CHECK(buffer->getFdCount() == 0);
    CHECK(buffer->flatten(writeBuffer, remaining, writeFds, fdCount) == 0);
    sp<GraphicBuffer> decoded = new GraphicBuffer();
    const void* readBuffer = flattened.data();
    const int* readFds = fdStorage;
    remaining = flattened.size();
    CHECK(decoded->unflatten(readBuffer, remaining, readFds, fdCount) == 0);
    CHECK(decoded->getId() == buffer->getId());
    CHECK(decoded->getGenerationNumber() == 42);
    checkGuard(storage);
    buffer->decStrong(storage);
    std::puts("PASS legacy handle constructor, 120-byte guard, ID/generation flattening");

    ANativeWindowBuffer native;
    native.common.incRef = nativeIncRef;
    native.common.decRef = nativeDecRef;
    native.width = 17;
    native.height = 9;
    native.stride = 32;
    native.format = 1;
    native.usage_deprecated = static_cast<int>(0xf0000001U);
    native.handle = nullptr;
    storage = allocateLegacyObject();
    legacyWindowCtor(storage, &native, false);
    buffer = static_cast<GraphicBuffer*>(storage);
    buffer->incStrong(storage);
    CHECK(wrappedReferences == 2);
    CHECK(buffer->getWidth() == 17 && buffer->getHeight() == 9);
    CHECK(buffer->getStride() == 32 && buffer->getLayerCount() == 1);
    CHECK(buffer->getUsage() == 0xf0000001ULL);
    checkGuard(storage);
    buffer->decStrong(storage);
    CHECK(wrappedReferences == 1);
    std::puts("PASS legacy native-window constructor, 120-byte guard, wrapped-buffer lifetime");

    // Exercise real HIDL scatter-gather replies containing native-handle FDs.
    // Constructor-only checks cannot detect an incompatible kernel FDA layout.
    const uint32_t cpuUsage = GraphicBuffer::USAGE_SW_READ_OFTEN
            | GraphicBuffer::USAGE_SW_WRITE_OFTEN;
    sp<GraphicBuffer> allocated = new GraphicBuffer(64, 64, 1, cpuUsage);
    std::printf("GraphicBuffer allocation initCheck=%d\n", allocated->initCheck());
    CHECK(allocated->initCheck() == 0);
    CHECK(allocated->handle != nullptr && allocated->handle->numFds > 0);
    CHECK(allocated->getWidth() == 64 && allocated->getHeight() == 64);
    void* pixels = nullptr;
    CHECK(allocated->lock(GraphicBuffer::USAGE_SW_WRITE_OFTEN, &pixels) == 0);
    CHECK(pixels != nullptr);
    const uint32_t stride = allocated->getStride();
    for (uint32_t y = 0; y < 64; ++y)
        for (uint32_t x = 0; x < 64; ++x)
            static_cast<uint32_t*>(pixels)[y * stride + x] = 0xff000000U | (y << 8) | x;
    CHECK(allocated->unlock() == 0);
    pixels = nullptr;
    CHECK(allocated->lock(GraphicBuffer::USAGE_SW_READ_OFTEN, &pixels) == 0);
    CHECK(pixels != nullptr);
    for (uint32_t y = 0; y < 64; ++y)
        for (uint32_t x = 0; x < 64; ++x)
            CHECK(static_cast<const uint32_t*>(pixels)[y * stride + x]
                    == (0xff000000U | (y << 8) | x));
    CHECK(allocated->unlock() == 0);
    allocated.clear();
    std::puts("PASS real HIDL native-handle FD transfer, allocation, CPU mapping and pixel readback");
    checkGrallocRetention();
    return 0;
}
