# Platform compatibility for stock mocha blobs

Run `bash device/xiaomi/mocha/apply-platform-patches.sh` from a LineageOS 15.1
source tree before building. The script verifies each patch before applying
it and recognizes patches that are already applied.

The GraphicBuffer patch restores the two constructor ABIs imported by the
stock HWC and OMX adaptor. It also retains a 120-byte GraphicBuffer on 32-bit
ARM. Disassembly of stock `hwcomposer.tegra.so`, at offset `0x100da`, shows
that its allocation is exactly 120 bytes; unmodified Android 8.1 needs 136.
A constructor alias alone would overwrite the caller's allocation.

Buffer IDs, generation numbers and the retained native-buffer reference live
in separately allocated private state. The public native-buffer layout and
flattened Binder representation stay the same. Source consumers of the C++
class must be rebuilt with the patched header; this is not a libui-only binary
replacement. The cost is one small additional allocation per GraphicBuffer.

Legacy handles retain their original WRAP_HANDLE/TAKE_HANDLE ownership, and
the ANativeWindowBuffer overload retains the wrapped buffer until release.
No Xiaomi proprietary binary is patched or replaced.

The GraphicsEnvironment patch adds an opt-out for the temporary thread used
to obtain an EGL display before an activity starts. Mocha enables it with
`ro.egl.disable_early_init=true`. On the stock driver, obtaining a display on
a thread and letting that thread exit makes subsequent initialization fail
with `EGL_BAD_DISPLAY`. The same calls succeed when that thread remains alive.
This was reproduced on hardware with both a native comparison and a real
debuggable app before applying the patch. The setting leaves hardware rendering
enabled and lets the render thread perform the normal EGL initialization.
Other devices keep the upstream early-init behavior by default.

The Gralloc0Mapper patch preserves the stock driver's handle reference count.
Tegra HWC retains a submitted handle by calling `registerBuffer`; the normal
HIDL mapper otherwise closes and deletes it when GraphicBuffer is released,
even if HWC is still using it. On wake this produced a use after free in
`NvGrUnregisterBuffer`. For modules exporting `NvGrFreeInternal`, the mapper
uses that entry point and delegates final handle cleanup to the driver.
It marks deletion pending and frees the handle at the last unregister.
Other modules retain the upstream cleanup path. This was tested on hardware
against the old path: retained FDs were prematurely closed before the fix,
while the vendor release path kept them valid through pixel readback and
closed them after the final unregister. The regular native test now exercises
this lifetime through the actual framework mapper.

The EglManager patch preserves thread state across hwui memory trims on
mocha, via the default-off ro.egl.skip_release_thread property. Independent
on-device tests created an ES2 context/pbuffer, rendered and read pixels,
then destroyed/unbound the context and surface before rebuilding. Keeping
both EGL states, calling only eglTerminate, or calling only eglReleaseThread
all passed. Only eglTerminate followed by eglReleaseThread made the next
eglChooseConfig fail with EGL_BAD_DISPLAY (even though eglInitialize returned
success). The patch skips only eglReleaseThread; display, context and pbuffer
cleanup still runs. This replaces OTA-17's unnecessarily broad display retention
and incomplete nonfatal initialization/surface branches. All upstream EGL
failure contracts remain intact. TaskSnapshotPersister retains a null check for
a failed hardware bitmap copy, which is already a supported return value.
Actual application trim/readback and cold-boot tests must accompany the probe.

The separate Recents allocation failure was a kernel Binder lifetime bug:
a temporary thread first opened hwbinder, exited, then a living thread could
no longer receive FD-array replies. The old binder_proc held the dead opener
instead of the process leader for files/mm. GraphicBuffer mapped this transport
failure to NO_RESOURCES/ENOMEM, even with ample free memory. See the kernel
Documentation/android/binder-thread-lifetime.txt and the three on-device
main/alive-worker/exited-worker comparisons. Adding memory or swallowing EGL
errors does not correct the failed IPC.

Tested baselines: frameworks/native `c6d109f4e3cdb41d6a6601b825c420e2731af59a`,
frameworks/base `39c4a924c7ed9f6093c0983de947734151e4bc7e`,
hardware/interfaces `5f14a29297db529a6f82527d8444af8ff0f65476`.
