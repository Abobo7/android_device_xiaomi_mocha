# Stock blob compatibility

`libmocha_atomic` restores the six `android_atomic_*` functions imported by
the Xiaomi stock blobs. Its return values and memory ordering match
LineageOS `android_system_core`, branch `cm-14.1`,
`include/cutils/atomic.h`.

The linker injects this library into `/system/lib/libcutils.so`, the former
provider of these functions. All six importing stock binaries have a direct
`DT_NEEDED` dependency on `libcutils.so`: camera.vendor.tegra, tegrastats,
gralloc.tegra, hwcomposer.tegra, libnvgr and libussrd. This preserves that
dependency route without modifying any proprietary binary. The shim itself
depends only on libc, so it does not introduce a libcutils dependency cycle.

In particular, increment/decrement return the value **before** the operation.
Release-load and acquire-store intentionally keep the legacy full fence;
they must not be replaced by invalid C/C++ release loads or acquire stores.
