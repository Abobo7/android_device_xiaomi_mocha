// SPDX-License-Identifier: Apache-2.0
// Stock Tegra nvmap accounting. Allocation usage tags do not exist on this
// kernel: report nvmap under OTHER, without inventing GL/graphics categories.
#define LOG_TAG "mocha-memtrack"
#include <android/hardware/memtrack/1.0/IMemtrack.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include <array>
#include <string>

using namespace android::hardware;
using namespace android::hardware::memtrack::V1_0;
struct Allocation {
    uint64_t size = 0, unaccounted = 0;
    unsigned system = 0, secure = 0;
    std::set<int32_t> owners;
};
struct Memtrack final : IMemtrack {
    Return<void> getMemory(int32_t pid, MemtrackType type, getMemory_cb cb) override {
        hidl_vec<MemtrackRecord> records;
        if (pid < 0) { cb(MemtrackStatus::MEMORY_TRACKING_NOT_SUPPORTED, records); return Void(); }
        if (type != MemtrackType::OTHER) {
            if (type >= MemtrackType::NUM_TYPES) cb(MemtrackStatus::TYPE_NOT_SUPPORTED, records);
            else cb(MemtrackStatus::SUCCESS, records); // all nvmap is assigned to OTHER
            return Void();
        }
        std::ifstream input("/sys/kernel/debug/nvmap/memtrack");
        std::string line;
        if (!std::getline(input, line) || line != "nvmap_memtrack_v1") {
            ALOGE("Cannot read stock nvmap accounting snapshot");
            cb(MemtrackStatus::MEMORY_TRACKING_NOT_SUPPORTED, records); return Void();
        }
        std::map<uint64_t, Allocation> allocations;
        while (std::getline(input, line)) {
            std::istringstream row(line);
            uint64_t id, size, unaccounted;
            int32_t owner;
            unsigned system, secure;
            std::string extra;
            if (!(row >> id >> owner >> size >> unaccounted >> system >> secure) ||
                    (row >> extra) || unaccounted > size || system > 1 || secure > 1) {
                ALOGE("Malformed nvmap accounting snapshot");
                cb(MemtrackStatus::MEMORY_TRACKING_NOT_SUPPORTED, records); return Void();
            }
            auto result = allocations.emplace(id, Allocation{size, unaccounted, system, secure, {}});
            Allocation& a = result.first->second;
            if (a.size != size || a.unaccounted != unaccounted || a.system != system || a.secure != secure) {
                cb(MemtrackStatus::MEMORY_TRACKING_NOT_SUPPORTED, records); return Void();
            }
            if (owner > 0) a.owners.insert(owner);
        }
        if (input.bad() || !input.eof()) {
            cb(MemtrackStatus::MEMORY_TRACKING_NOT_SUPPORTED, records); return Void();
        }
        // Fixed bucket count. Split by distinct owner PID, not FD/refcount.
        // Kernel references are not additional userspace processes. Mapped system pages
        // stay in smaps; only globally unmapped pages augment process PSS.
        std::array<uint64_t, 4> bytes{};
        for (const auto& item : allocations) {
            const Allocation& a = item.second;
            if (a.owners.count(pid)) bytes[a.system * 2 + a.secure] += a.unaccounted / a.owners.size();
        }
        records.resize(bytes.size());
        for (unsigned i = 0; i < bytes.size(); ++i) {
            records[i].sizeInBytes = bytes[i];
            records[i].flags = static_cast<uint32_t>(MemtrackFlag::SMAPS_UNACCOUNTED) |
                static_cast<uint32_t>(MemtrackFlag::SHARED_PSS) |
                static_cast<uint32_t>(i & 2 ? MemtrackFlag::SYSTEM : MemtrackFlag::DEDICATED) |
                static_cast<uint32_t>(i & 1 ? MemtrackFlag::SECURE : MemtrackFlag::NONSECURE);
        }
        cb(MemtrackStatus::SUCCESS, records);
        return Void();
    }
};
int main() {
    configureRpcThreadpool(1, true);
    android::sp<Memtrack> service = new Memtrack;
    if (service->registerAsService() != android::OK) return 1;
    joinRpcThreadpool();
    return 1;
}