// Exercise the fixed-size allocations used by stock NVIDIA Binder clients.
#include <binder/Parcel.h>
#include <utils/String16.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

using android::Parcel;
using android::String16;

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(2);
    }
}

int main() {
    static_assert(sizeof(Parcel) == 48, "stock ARM Parcel allocation is 48 bytes");
    struct Slot {
        uint32_t before[4];
        alignas(Parcel) unsigned char storage[48];
        uint32_t after[4];
    } slot;
    auto check = [&]() {
        for (unsigned i = 0; i < 4; ++i) {
            require(slot.before[i] == 0x13579bdf, "write before old Parcel allocation");
            require(slot.after[i] == 0x2468ace0, "write after old Parcel allocation");
        }
    };
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        for (unsigned i = 0; i < 4; ++i) {
            slot.before[i] = 0x13579bdf;
            slot.after[i] = 0x2468ace0;
        }
        auto* parcel = new (slot.storage) Parcel;
        check();
        require(parcel->writeInt32(123456) == android::NO_ERROR, "write integer");
        require(parcel->writeString16(String16("mocha")) == android::NO_ERROR, "write string");
        parcel->setDataPosition(0);
        require(parcel->readInt32() == 123456, "read integer");
        require(parcel->readString16() == String16("mocha"), "read string");
        parcel->freeData();
        require(!parcel->replaceCallingWorkSourceUid(1234), "new parcel has no work-source header");
        require(parcel->writeInterfaceToken(String16("mocha.abi")) == android::NO_ERROR,
                "write interface token");
        const auto position = parcel->dataPosition();
        require(parcel->replaceCallingWorkSourceUid(1234), "replace work-source UID");
        require(parcel->readCallingWorkSourceUid() == 1234, "read work-source UID");
        require(parcel->dataPosition() == position, "work-source access preserves position");
        parcel->freeData();
        require(!parcel->replaceCallingWorkSourceUid(5678), "freeData resets work-source state");
        {
            Parcel::WritableBlob blob;
            require(parcel->writeBlob(32768, false, &blob) == android::NO_ERROR, "write ashmem blob");
            std::memset(blob.data(), 0x5a, blob.size());
            require(parcel->getOpenAshmemSize() == 32768, "ashmem accounting");
        }
        parcel->setDataPosition(0);
        {
            Parcel::ReadableBlob blob;
            require(parcel->readBlob(32768, &blob) == android::NO_ERROR, "read ashmem blob");
            const auto* bytes = static_cast<const unsigned char*>(blob.data());
            require(bytes[0] == 0x5a && bytes[32767] == 0x5a, "ashmem contents");
        }
        parcel->freeData();
        require(parcel->getOpenAshmemSize() == 0, "freeData releases ashmem accounting");
        check();
        parcel->~Parcel();
        check();
    }
    std::puts("PASS: 48-byte Parcel canaries, data roundtrip, work-source state and ashmem lifecycle (100 cycles)");
    return 0;
}
