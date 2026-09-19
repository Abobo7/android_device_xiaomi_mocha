/*
 * Copyright (C) 2013 The Android Open Source Project
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

#ifndef SENSOREVENTQUEUE_H_
#define SENSOREVENTQUEUE_H_

#include <hardware/sensors.h>
#include <pthread.h>

/*
 * Fixed-size circular queue. Legacy HALs must poll into a separate buffer:
 * they may write a whole sensor pair even when a ring-buffer tail has one slot.
 *
 * Thread safety:
 * All access requires the caller's mutex. There can only be one writer at a
 * time. write() releases that mutex while waiting for the reader to free space.
 */
class SensorEventQueue {
    int mCapacity;
    int mStart; // start of readable region
    int mSize; // number of readable items
    int mPendingSize; // polled items waiting for space in the ring
    sensors_event_t* mData;
    pthread_cond_t mSpaceAvailableCondition;

public:
    SensorEventQueue(int capacity);
    ~SensorEventQueue();

    // Returns length of region, between zero and min(capacity, requestedLength). If there is any
    // writable space, it will return a region of at least one. Because it must return
    // a pointer to a contiguous region, it may return smaller regions as we approach the end of
    // the data array.
    // Only call while holding the lock.
    // The region is not marked internally in any way. Subsequent calls may return overlapping
    // regions. This class expects there to be exactly one writer at a time.
    int getWritableRegion(int requestedLength, sensors_event_t** out);

    // After writing to the region returned by getWritableRegion(), call this to indicate how
    // many records were actually written.
    // This increases size() by count.
    // Only call while holding the lock.
    void markAsWritten(int count);

    // Copy a validated poll batch, splitting at ring boundaries and waiting
    // for space as needed. Call with mutex held; it is also held on return.
    // Signal readers after each chunk, before possibly waiting for space.
    void write(const sensors_event_t* events, int count, pthread_mutex_t* mutex,
            pthread_cond_t* dataAvailable);

    // Gets the number of readable records.
    // Only call while holding the lock.
    int getSize();

    // Readable plus already-polled items awaiting ring space. A flush barrier
    // must include both, even if write() is waiting with the mutex released.
    // Only call while holding the lock.
    int getPendingSize();

    // Returns pointer to the first readable record, or NULL if size() is zero.
    // Only call this while holding the lock.
    sensors_event_t* peek();

    // This will decrease the size by one, freeing up the oldest readable event's slot for writing.
    // Only call while holding the lock.
    void dequeue();

    // Blocks until space is available. No-op if there is already space.
    // Returns true if it had to wait.
    bool waitForSpace(pthread_mutex_t* mutex);
};

#endif // SENSOREVENTQUEUE_H_
