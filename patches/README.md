# Platform compatibility for stock mocha blobs

## Published baseline verification (2026-10-01)

`BASELINES.json` records each patched platform repository's clean commit and
the SHA-256 of each patch in application order. The publication check replayed
these patches against an isolated Git index at each clean commit and verified
the resulting source files against the working tree. This check does not alter
platform source or substitute for the recorded runtime validation.

Run `apply-platform-patches.sh` after repo sync. It also verifies and restores
the validated WebView prebuilt via `prepare-webview.sh`. The original hardware
blobs remain unchanged.


Run `bash device/xiaomi/mocha/apply-platform-patches.sh` from the active LineageOS
source tree before building. The script verifies each patch before applying
it and recognizes patches that are already applied. Use the patch set from the branch matching the Android source version.

Bionic Fortify normally checks for O_TMPFILE when validating open flags. The
mocha kernel exports older UAPI headers that omit that flag, so the guard
falls back to checking O_CREAT when O_TMPFILE is unavailable. Current headers
retain the full upstream O_TMPFILE check.

The NVIDIA power HAL is pinned to its 15.1 branch because the 16.0 branch
replaces the legacy `power.tegra` module with a HIDL implementation. Its
16.0 compatibility patch keeps GNU-designator and unused-code diagnostics
visible without letting Android 9 Clang's `-Werror` policy reject the legacy HAL. It
removes the retired `POWER_HINT_SET_PROFILE` enum from the hint table (its handler was
already a no-op) and sizes the table for the current Android 9 enum.

The GraphicBuffer patch restores the two constructor ABIs imported by the
stock HWC and OMX adaptor. It also retains a 120-byte GraphicBuffer on 32-bit
ARM. Disassembly of stock `hwcomposer.tegra.so`, at offset `0x100da`, shows
that its allocation is exactly 120 bytes; unmodified Android 8.1 needs 136.
A constructor alias alone would overwrite the caller's allocation.

Buffer IDs, generation numbers and the retained native-buffer reference live
in separately allocated private state. The BufferState constructor and destructor
are out of line so its unique_ptr<DetachedBufferHandle> member is destroyed where
the handle type is complete. The public native-buffer layout and
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

The SurfaceFlinger client-composition option defaults off. Mocha enables
ro.sf.force_client_composition to avoid stale pages and notification shade
flicker through stock HWC 1.1 on the Pie HWC2On1 adapter. On-device testing
showed that switching Skia GL to HWUI GL alone did not fix either symptom;
forcing client composition did, including after restoring Skia GL. The
option initializes the same state as SurfaceFlinger debug transaction 1008.
GPU hardware rendering remains enabled, and stock HWC still handles display
presentation and vsync. Increased GPU composition work/power is a limitation;
this is a compatibility fallback, not a claim to repair the proprietary HAL.
SetupWizard source is unchanged. Installed OTA boot verification passed: the
flag initializes automatically, the renderer override is empty, and SystemUI
uses Skia GL. The user confirmed normal page and notification interactions.


## Pie runtime follow-ups

Fence default construction/destruction is out of line again to export the
legacy symbols required by the stock camera HAL. Ownership and layout retain
Pie unique_fd semantics. The camera provider separately opts into pre-M linker
compatibility with ro.camera.legacy_text_relocations; stock libFaceProc.so has
text relocations. This setting is process scoped and defaults off.

The factory conn_init program rewrites persist.service.bdroid.bdaddr with
unpadded octets and a trailing newline on every boot. The Bluetooth address
fallback parser accepts that format only with ro.bluetooth.legacy_bdaddr;
normal parsing is attempted first, invalid and zero/broadcast addresses are
rejected, and no device address is embedded in the ROM. Host ASan/UBSan tests
cover valid and invalid inputs. The V4L2 libbt paths restore the Oreo division
of work: UIM owns power/firmware setup. Device configuration also corrects the
UIM executable path, bcm_ldisc sysfs prefix, and HCI line discipline 26.
Installed v2 reached Bluetooth ON with zero crashes; pairing/audio remain
unverified. Both cameras, saved photos, video saving and return to photography
passed user testing with the temporary FUSE storage backend.

Pie removed the userspace FUSE implementation from sdcard, but vold still
supports its mount lifecycle. The stock kernel has FUSE and no sdcardfs/esdfs.
The system/core dispatcher opts into device module sdcard-fuse only when
ro.sys.legacy_fuse=true; mocha sets ro.sys.sdcardfs=false. The module imports
the validated Oreo sources, preserves licenses, and accepts Pie's sdcardfs-only
-i flag as a no-op. Source revision and hashes are in sdcard-fuse/README.md.
The normal vold-managed mount path is retained and no data migration is needed.

Installed v3 boots with FUSE mounted automatically, no persistent override or
diagnostic bind mount. The user confirmed photo saving and viewing after
that reboot. Temporary files/properties were removed and ADB unrooted.


## Skia GL hardware bitmaps on the stock NVIDIA driver

NVIDIA Tegra 334.00 reports GLES 3.1 / ESSL 3.10 and
GL_OES_EGL_image_external, but no GL_OES_EGL_image_external_essl3.
Skia chooses an ES 3.x shader generation, GrGLCaps consequently disables
externalTextureSupport, and GrGLGpu::check_backend_texture rejects the
GL_TEXTURE_EXTERNAL_OES images used for Android hardware bitmaps. Trebuchet
decodes its cached icons as Bitmap.Config.HARDWARE, explaining why those icons
disappear while other drawing paths continue to work. Skipping prepareToDraw
does not fix this.

The patch selects ESSL 1.00 only for NVIDIA GLES contexts with that precise
extension mismatch. It retains Skia GL, the GLES context, GPU rendering and
hardware bitmap storage. Desktop GL and drivers with the ES3 extension retain
their original shader generation. The same compatibility principle appears in
newer upstream Skia's fPreferExternalImagesOverES3 option.

On-device isolation: ESSL 1.00 external-image shader compilation succeeds;
ESSL 3.00 with the ES3 extension fails explicitly. Instrumented uploads report
GL_NO_ERROR, valid source pixels and normal pixel-store state. A controlled
Skia-library comparison changes externalTextureSupport from false to true.
All nine copy/decode/Picture hardware-bitmap round trips (123, 128 and 256 px)
then match the source pixels exactly, with the Skia GL renderer retained.
Runtime screenshot and installed-ROM verification are recorded in the handover.

The companion patch keys persistent GL program binaries by a format tag, GL/GLES
standard, and selected GLSL generation before the existing program descriptor.
Both cache load and store use the same helper. The in-memory descriptor remains
context-local. This is necessary because the old descriptor omits shader language
and Android's FileBlobCache only invalidates on ro.build.id, which remains
PQ3A.190801.002 across these builds. No application or shader cache is cleared.

With both patches and the existing on-device caches retained, desktop icons,
clock shadows, the application drawer, notifications, and Settings render
correctly. Restarting Trebuchet and reusing the disk cache also passes. Captured
cache files retain all 23 old launcher keys and 21 old SystemUI keys alongside
the new namespace. Fifteen launcher and seventeen SystemUI entries have the same
old descriptor but different program binaries, confirming that they need distinct
persistent keys. Nine hardware-bitmap readbacks still match every source pixel.
Evidence: diagnostics/skia-desktop-20260927-1030 in the handover repository.
