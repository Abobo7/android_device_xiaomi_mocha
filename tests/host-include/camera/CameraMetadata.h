// Host-only ownership shim for the camera lifecycle test. The metadata
// allocator/clone/free/find functions are the real Android C implementation.
// This is not installed in the ROM and does not model Binder or normalization.
#pragma once
#include <assert.h>
#include <errno.h>
#include <system/camera_metadata.h>

namespace android {
class CameraMetadata {
    camera_metadata_t* buffer = nullptr;
    mutable bool locked = false;
public:
    CameraMetadata() = default;
    CameraMetadata(const CameraMetadata& other) {
        buffer = clone_camera_metadata(other.buffer);
    }
    ~CameraMetadata() { clear(); }
    CameraMetadata& operator=(const CameraMetadata& other) {
        return operator=(other.buffer);
    }
    CameraMetadata& operator=(const camera_metadata_t* other) {
        if (other != buffer) {
            camera_metadata_t* copy = clone_camera_metadata(other);
            clear();
            buffer = copy;
        }
        return *this;
    }
    void clear() {
        assert(!locked);
        free_camera_metadata(buffer);
        buffer = nullptr;
    }
    void acquire(camera_metadata_t* other) {
        clear();
        buffer = other;
    }
    bool isEmpty() const {
        return !buffer || get_camera_metadata_entry_count(buffer) == 0;
    }
    const camera_metadata_t* getAndLock() const {
        assert(!locked);
        locked = true;
        return buffer;
    }
    int unlock(const camera_metadata_t* other) const {
        assert(locked && other == buffer);
        locked = false;
        return 0;
    }
    camera_metadata_entry_t find(uint32_t tag) {
        camera_metadata_entry_t entry = {};
        find_camera_metadata_entry(buffer, tag, &entry);
        return entry;
    }
    template<typename T>
    int update(uint32_t tag, const T* data, size_t count) {
        assert(!locked);
        if (!buffer) buffer = allocate_camera_metadata(64, 4096);
        if (!buffer) return -ENOMEM;
        camera_metadata_entry_t entry = {};
        if (find_camera_metadata_entry(buffer, tag, &entry) == 0) {
            return update_camera_metadata_entry(buffer, entry.index, data, count, nullptr);
        }
        return add_camera_metadata_entry(buffer, tag, data, count);
    }
};
}  // namespace android
