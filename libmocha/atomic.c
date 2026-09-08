/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>

/*
 * Stock camera, graphics and power blobs import these former libcutils APIs.
 * Preserve the cm-14.1 cutils/atomic.h return values and memory ordering,
 * including its full barriers for release-load and acquire-store.
 */
int32_t android_atomic_inc(volatile int32_t *addr)
{
    return __atomic_fetch_add(addr, 1, __ATOMIC_RELEASE);
}

int32_t android_atomic_dec(volatile int32_t *addr)
{
    return __atomic_fetch_sub(addr, 1, __ATOMIC_RELEASE);
}

int32_t android_atomic_acquire_load(volatile const int32_t *addr)
{
    return __atomic_load_n(addr, __ATOMIC_ACQUIRE);
}

int32_t android_atomic_release_load(volatile const int32_t *addr)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return __atomic_load_n(addr, __ATOMIC_RELAXED);
}

void android_atomic_acquire_store(int32_t value, volatile int32_t *addr)
{
    __atomic_store_n(addr, value, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

void android_atomic_release_store(int32_t value, volatile int32_t *addr)
{
    __atomic_store_n(addr, value, __ATOMIC_RELEASE);
}
