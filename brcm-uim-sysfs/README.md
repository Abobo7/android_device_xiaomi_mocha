# Broadcom UIM for mocha Bluetooth

Imported from LineageOS/android_hardware_broadcom_fm, lineage-16.0,
commit `c7fa50b7b16d847cfbecdc315b21fdb968bffbe8`, directory `brcm-uim-sysfs`.
Original source headers and build flags are preserved. No FM application is required.
UIM owns power/firmware and the V4L2 line discipline used by the stock Bluetooth stack.
The module installs to /vendor/bin; BoardConfig supplies the bcm_ldisc path and HCI 26.
The original file SHA-256 values are recorded in UPSTREAM-SHA256.json.
