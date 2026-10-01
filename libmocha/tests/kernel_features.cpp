#include <errno.h>
#include <fcntl.h>
#include <ion/ion.h>
#include <linux/memfd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s (errno=%d %s)\n", __LINE__, #x, errno, strerror(errno)); exit(1); } } while (0)

static int new_memfd(unsigned flags) {
    return syscall(__NR_memfd_create, "mocha-jit-test", flags);
}

static void memfd_test() {
    const size_t page = getpagesize();
    int plain = new_memfd(0);
    CHECK(plain >= 0);
    CHECK(fcntl(plain, F_GET_SEALS) == F_SEAL_SEAL);
    CHECK(fcntl(plain, F_ADD_SEALS, F_SEAL_SHRINK) == -1 && errno == EPERM);
    close(plain);
    CHECK(new_memfd(0x80000000U) == -1 && errno == EINVAL);
    for (int round = 0; round < 64; ++round) {
        int fd = new_memfd(MFD_CLOEXEC | MFD_ALLOW_SEALING);
        CHECK(fd >= 0);
        CHECK(fcntl(fd, F_GETFD) & FD_CLOEXEC);
        CHECK(ftruncate(fd, page * 2) == 0);
        // Match ART: independent mappings of data and executable code offsets.
        auto data = static_cast<uint32_t*>(mmap(nullptr, page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
        auto rw = static_cast<uint32_t*>(mmap(nullptr, page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, page));
        auto rx = static_cast<uint32_t*>(mmap(nullptr, page, PROT_READ | PROT_EXEC, MAP_SHARED, fd, page));
        CHECK(data != MAP_FAILED && rw != MAP_FAILED && rx != MAP_FAILED);
        data[0] = 0xaabbccdd;
        rw[0] = 0xe3a0002a; // ARM mov r0,#42
        rw[1] = 0xe12fff1e; // ARM bx lr
        __builtin___clear_cache(reinterpret_cast<char*>(rw), reinterpret_cast<char*>(rw + 2));
        __builtin___clear_cache(reinterpret_cast<char*>(rx), reinterpret_cast<char*>(rx + 2));
        auto fn = reinterpret_cast<int (*)()>(rx);
        CHECK(fn() == 42);
        rw[0] = 0xe3a00049; // Update compiled code through the writable alias.
        __builtin___clear_cache(reinterpret_cast<char*>(rw), reinterpret_cast<char*>(rw + 2));
        __builtin___clear_cache(reinterpret_cast<char*>(rx), reinterpret_cast<char*>(rx + 2));
        CHECK(fn() == 73 && data[0] == 0xaabbccdd);
        CHECK(fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE) == -1 && errno == EBUSY);
        int copy = dup(fd);
        CHECK(copy >= 0);
        pid_t child = fork();
        CHECK(child >= 0);
        if (!child) { data[0] = 0x11223344; _exit(fn() == 73 ? 0 : 2); }
        int status;
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        CHECK(data[0] == 0x11223344);
        CHECK(munmap(data, page) == 0 && munmap(rw, page) == 0 && munmap(rx, page) == 0);
        CHECK(fcntl(copy, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW) == 0);
        uint32_t value = 1;
        CHECK(pwrite(fd, &value, sizeof(value), 0) == -1 && errno == EPERM);
        CHECK(ftruncate(fd, page) == -1 && errno == EPERM);
        CHECK(ftruncate(fd, page * 3) == -1 && errno == EPERM);
        CHECK(mmap(nullptr, page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) == MAP_FAILED && errno == EPERM);
        // Reopen read-only: an O_RDWR shared mapping retains VM_MAYWRITE on 3.17.
        char path[64];
        snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
        int readfd = open(path, O_RDONLY | O_CLOEXEC);
        CHECK(readfd >= 0);
        void* ro = mmap(nullptr, page, PROT_READ, MAP_SHARED, readfd, 0);
        CHECK(ro != MAP_FAILED);
        CHECK(mprotect(ro, page, PROT_READ | PROT_WRITE) == -1 && errno == EACCES);
        CHECK(*static_cast<uint32_t*>(ro) == 0x11223344);
        CHECK(munmap(ro, page) == 0);
        close(readfd);
        CHECK(fcntl(fd, F_ADD_SEALS, F_SEAL_SEAL) == 0);
        CHECK(fcntl(copy, F_GET_SEALS) == (F_SEAL_SEAL | F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW));
        CHECK(fcntl(copy, F_ADD_SEALS, F_SEAL_WRITE) == -1 && errno == EPERM);
        close(copy);
        close(fd);
    }
    puts("PASS memfd: 64 dual-view ARM execution/update, fork sharing and seal enforcement cycles");
}

static void ion_test() {
    int ion = ion_open();
    CHECK(ion >= 0 && ion_is_legacy(ion));
    for (int round = 0; round < 64; ++round) {
        size_t size = (1 + round % 8) * 4096;
        int fd = -1;
        CHECK(ion_alloc_fd(ion, size, 0, 1, ION_FLAG_CACHED, &fd) == 0 && fd >= 0);
        auto mem = static_cast<uint8_t*>(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
        CHECK(mem != MAP_FAILED);
        memset(mem, 0x5a, size);
        CHECK(ion_sync_fd(ion, fd) == 0);
        pid_t child = fork();
        CHECK(child >= 0);
        if (!child) {
            CHECK(munmap(mem, size) == 0);
            close(ion);
            int other = ion_open();
            CHECK(other >= 0);
            ion_user_handle_t handle;
            CHECK(ion_import(other, fd, &handle) == 0);
            int shared = -1;
            CHECK(ion_share(other, handle, &shared) == 0);
            close(fd);
            auto second = static_cast<uint8_t*>(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, shared, 0));
            CHECK(second != MAP_FAILED);
            for (size_t i = 0; i < size; ++i) CHECK(second[i] == 0x5a);
            memset(second, 0xa5, size);
            CHECK(ion_sync_fd(other, shared) == 0);
            CHECK(munmap(second, size) == 0);
            close(shared);
            CHECK(ion_free(other, handle) == 0);
            close(other);
            _exit(0);
        }
        int status;
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        CHECK(ion_sync_fd(ion, fd) == 0);
        for (size_t i = 0; i < size; ++i) CHECK(mem[i] == 0xa5);
        CHECK(munmap(mem, size) == 0);
        close(fd);
    }
    close(ion);
    puts("PASS ION: 64 system heap allocation, mmap, independent client import/share and cache sync cycles");
}

int main(int argc, char** argv) {
    setbuf(stdout, nullptr);
    if (argc == 1 || !strcmp(argv[1], "memfd")) memfd_test();
    if (argc == 1 || !strcmp(argv[1], "ion")) ion_test();
    return 0;
}
