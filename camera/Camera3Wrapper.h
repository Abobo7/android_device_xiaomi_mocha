/* Copyright (C) 2015 The CyanogenMod Project
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <hardware/camera3.h>

int camera3_device_open(const hw_module_t* module, const char* name, hw_device_t** device);
