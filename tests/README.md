# Host regression tests

Run in a built LineageOS tree with GNU 32-bit C/C++ development libraries:

    bash device/xiaomi/mocha/tests/run-host-tests.sh /tmp/mocha-host-tests

The tests compile the actual adapter sources with AddressSanitizer,
UndefinedBehaviorSanitizer and leak checking. Outputs are retained in the
chosen directory (or a new temporary directory if no argument is supplied).
Nothing is installed on Android.

- Sensors: actual multihal writer, queue, handle remapping and software flush.
  A scripted legacy HAL checks full-sized poll buffers, ring tails, wraparound,
  backpressure, invalid returns, flush ordering/wakeup and 52,000 ordered events.
- Camera: actual Camera3Wrapper.cpp, fake stock HAL/gralloc, actual Android C
  metadata allocation/clone/free, and a small host-only CameraMetadata ownership
  shim. Checks template lifetime across successful/failed internal restarts,
  unchanged geometry handling, and dump/configure/close concurrency.
  The production 32-bit ABI assertions remain enabled.

These are logic and memory-safety tests, not real-device hardware validation.
Camera request normalization, Binder/HIDL, ARM execution, capture buffers and
the proprietary HAL are not modeled. Compile the real Android modules as well;
then test recording -> stop -> photo -> recording on the device.
