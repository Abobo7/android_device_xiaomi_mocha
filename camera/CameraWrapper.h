/* Copyright (C) 2015 The CyanogenMod Project
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <hardware/camera_common.h>

camera_module_t* get_stock_camera_module();
int get_mocha_camera_info(int id, camera_info* info);
