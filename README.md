# Xiaomi Mi Pad (mocha): Android 10 / LineageOS 17.1 r15

r15 boots with JIT and Codec2 enabled. Graphics/Parcel ABI compatibility,
FUSE storage, Wi-Fi, camera, H.264 decoding, memory accounting, and the EGL
preload lifetime repairs are included. The final installed r15 ZIP SHA-256 is
`5ecd828d80093517c024811c168d00f5bbdef137f41e6d7a75c7aac719599336`.
An old memtrack crash is not proven fixed; FMRadio is absent in this Q baseline,
and audio peripherals and long-term stability remain incompletely tested.

## Rebuilding the validated branch

Initialize the official LineageOS manifest on `lineage-17.1`, then install
`manifests/mocha.xml` from this branch as `.repo/local_manifests/mocha.xml`
before syncing. Do not add duplicate mocha entries or `device/nvidia/tegra-common`.
The device, kernel, and vendor projects must all use `lineage-17.1`.

Before building, run `bash device/xiaomi/mocha/apply-platform-patches.sh`.
This verifies/applies the checked-in platform patches and restores the exact
WebView prebuilt from `vendor/xiaomi/mocha/prebuilt/webview`.
Use the pinned Linaro 4.9.4 toolchain for the kernel. Export `LINEAGE_BUILD=mocha`
before sourcing `build/envsetup.sh`, run `breakfast mocha`, then build `bacon`.
`MOCHA_ADB_PUBLIC_KEY` is optional and may point to the builder's own public key;
no private key or device-specific authorization key is stored here.

Platform changes are distributed as device-tree patches, not pushed to LineageOS
upstream repositories. Patch baselines are recorded under `patches/`.
Keep recovery and user data when installing a compatible update; no wipe is
part of these source publication changes. Existing proprietary blob bytes are
unchanged. Consult `patches/README.md` for the compatibility details.

# Device configuration for XiaoMi MiPad Tablet

## Spec Sheet
| Feature                 | Specification                     |
| :---------------------- | :-------------------------------- |
| CPU                     | Quad Core 2.2GHz                  |
| Chipset                 | NVIDIA® Tegra K1 T124             |
| GPU                     | NVIDIA® GK20A (Kepler)            |
| Memory                  | 2GB RAM                           |
| Shipped Android Version | 4.4.2                             |
| Storage                 | 16/32/64GB                           |
| MicroSD                 | Up to 128GB                       |
| Battery                 | 5197 mAh                          |
| Dimentions              | 221 x 126 x 9.2 mm                |
| Display                 | 1536 x 2048 pixels                |
| Release Date            | July 29, 2014                     |

## Device Picture
![Nvidia SHIELD Tablet ](http://shield.nvidia.co.uk/images/home-page-sections/shield-tablet-controller-header-image.png "Nvidia SHIELD Tablet")

## Copyright

```
#
# Copyright (C) 2015 The CyanogenMod Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
```
