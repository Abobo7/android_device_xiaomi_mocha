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

The EglManager patch works around two legacy Tegra EGL driver behaviors that
soft-reboot the device. Root cause measured on hardware (2026-09-09): when an
app launches, its snapshot starting window makes system_server initialize
hwui EGL; a later TRIM_MEMORY_COMPLETE (memory pressure) runs
EglManager::destroy(), whose eglTerminate() permanently invalidates the
default display on this driver; the next task-snapshot persistence readback
(Bitmap.copy of a hardware snapshot) then re-initializes EGL and
eglChooseConfig fails with EGL_BAD_DISPLAY, hitting LOG_ALWAYS_FATAL in
loadConfigs() and killing system_server. This reproduced within one minute
of boot by opening and back-exiting any app (camera, browser) while cached
apps were being killed. The same createSurface abort also killed SystemUI
when opening Recents under memory pressure: nvwsi (the Tegra EGL window
wrapper) fails dequeueBuffer with ENOMEM and reports EGL_BAD_NATIVE_WINDOW,
which crashed the SystemUI RenderThread in a loop and bounced the user to
the lockscreen. Mocha enables two opt-in properties:
ro.egl.keep_display_initialized skips eglTerminate/eglReleaseThread in
EglManager::destroy() (the context and pbuffer surface are still destroyed,
which is what frees GPU memory, and re-initialization then succeeds);
ro.egl.nonfatal_init converts the loadConfigs()/createSurface() aborts into
logged errors so a transient failure skips one snapshot persistence or one
recents attempt instead of killing the process (a single reopen recovers,
verified on hardware). TaskSnapshotPersister also treats a
failed hardware readback as a skipped snapshot. Other devices keep the
upstream fatal behavior.

Tested baselines: frameworks/native `c6d109f4e3cdb41d6a6601b825c420e2731af59a`,
frameworks/base `39c4a924c7ed9f6093c0983de947734151e4bc7e`,
hardware/interfaces `5f14a29297db529a6f82527d8444af8ff0f65476`.
