// Exercise the actual adapter/writer/queue using a scripted legacy poll.
#undef NDEBUG
#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

// multihal includes this old C11 header but never uses it; it is not a
// dependency of this host test (GNU C++ does not expose its C atomic typedefs).
#define ANDROID_CUTILS_ATOMIC_H
#include "../sensors/multihal.cpp"
#include "../sensors/SensorEventQueue.cpp"

static sensors_event_t event(int sequence) {
    sensors_event_t result = {};
    result.version = sizeof(result);
    result.sensor = 101;
    result.type = SENSOR_TYPE_ACCELEROMETER;
    result.timestamp = sequence;
    return result;
}

template<typename Predicate>
static void wait_until(Predicate ready) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!ready()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

static void test_empty_tail_and_backpressure(int capacity) {
    SensorEventQueue queue(capacity);
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t available = PTHREAD_COND_INITIALIZER;
    std::vector<sensors_event_t> input(79);
    for (size_t i = 0; i < input.size(); ++i) input[i] = event(i);
    pthread_mutex_lock(&mutex);
    // Leave an empty queue whose write pointer is at the last slot.
    queue.write(input.data(), capacity - 1, &mutex, &available);
    for (int i = 0; i < capacity - 1; ++i) queue.dequeue();
    assert(queue.getSize() == 0);
    pthread_mutex_unlock(&mutex);
    std::thread writer([&] {
        pthread_mutex_lock(&mutex);
        queue.write(input.data(), input.size(), &mutex, &available);
        assert(queue.getPendingSize() == queue.getSize());
        pthread_mutex_unlock(&mutex);
    });
    for (size_t i = 0; i < input.size(); ++i) {
        pthread_mutex_lock(&mutex);
        while (!queue.peek()) pthread_cond_wait(&available, &mutex);
        assert(queue.getSize() <= capacity);
        assert(queue.peek()->timestamp == static_cast<int64_t>(i));
        queue.dequeue();
        pthread_mutex_unlock(&mutex);
    }
    writer.join();
    assert(queue.getSize() == 0 && queue.getPendingSize() == 0);
    pthread_cond_destroy(&available);
    pthread_mutex_destroy(&mutex);
}

struct StopWriter {};
static std::mutex poll_mutex;
static std::condition_variable poll_ready;
static std::deque<int> replies;
static bool stop_writer = false;
static int next_sequence = 19;
static int poll_calls = 0;

static int fake_poll(sensors_poll_device_t*, sensors_event_t* buffer, int count) {
    // Reproduces the legacy pair behavior. The pre-fix writer offers count=1
    // at the ring tail; the fixed writer must always offer the full buffer.
    assert(count == SENSOR_EVENT_QUEUE_CAPACITY);
    std::unique_lock<std::mutex> lock(poll_mutex);
    poll_ready.wait(lock, [] { return stop_writer || !replies.empty(); });
    if (stop_writer) throw StopWriter();
    int result = replies.front();
    replies.pop_front();
    ++poll_calls;
    if (result > 0 && result <= count) {
        for (int i = 0; i < result; ++i) buffer[i] = event(next_sequence++);
    }
    // For the impossible oversized-return test, only the return value is
    // corrupted. Arbitrary vendor writes beyond count cannot be sandboxed.
    return result;
}

static int fake_activate(sensors_poll_device_t*, int, int) { return 0; }
static int fake_delay(sensors_poll_device_t*, int handle, int64_t ns) {
    assert(handle == 101 && ns == 20000000);
    return 0;
}
static int forbidden_batch(sensors_poll_device_1_t*, int, int, int64_t, int64_t) {
    abort();
}
static int forbidden_flush(sensors_poll_device_1_t*, int) { abort(); }

static void schedule(std::initializer_list<int> results) {
    std::lock_guard<std::mutex> lock(poll_mutex);
    replies.insert(replies.end(), results.begin(), results.end());
    poll_ready.notify_all();
}

static void test_adapter() {
    sensor_t specs[2] = {};
    specs[0].handle = assign_global_handle(0, 101);
    specs[0].type = SENSOR_TYPE_ACCELEROMETER;
    specs[1].handle = assign_global_handle(0, 102);
    specs[1].type = SENSOR_TYPE_SIGNIFICANT_MOTION;
    for (auto& spec : specs) {
        spec.flags = 0xffffffff;
        spec.fifoMaxEventCount = 999;
        describe_legacy_sensor(&spec);
        assert(spec.fifoMaxEventCount == 0 && spec.fifoReservedEventCount == 0);
    }
    assert(specs[0].flags == SENSOR_FLAG_CONTINUOUS_MODE);
    assert(specs[1].flags == (SENSOR_FLAG_ONE_SHOT_MODE | SENSOR_FLAG_WAKE_UP));
    global_sensors_list = specs;
    global_sensors_count = 2;
    const int handle = specs[0].handle;
    sensors_poll_device_1_t legacy = {};
    legacy.common.version = SENSORS_DEVICE_API_VERSION_1_1;
    legacy.activate = fake_activate;
    legacy.setDelay = fake_delay;
    legacy.poll = fake_poll;
    legacy.batch = forbidden_batch;
    legacy.flush = forbidden_flush;
    SensorEventQueue queue(SENSOR_EVENT_QUEUE_CAPACITY);
    sensors_poll_context_t ctx;
    ctx.nextReadIndex = 0;
    ctx.sub_hw_devices.push_back(&legacy.common);
    ctx.queues.push_back(&queue);
    ctx.consumed_events.push_back(0);
    assert(ctx.batch(handle, 0, 20000000, 100000000) == 0);
    assert(ctx.batch(handle, 1, 20000000, 0) == -EINVAL);
    assert(ctx.flush(handle) == -EINVAL);
    assert(ctx.activate(handle, 1) == 0);
    assert(ctx.activate(specs[1].handle, 1) == 0);
    assert(ctx.flush(specs[1].handle) == -EINVAL);

    // Prime 19 slots, then consume 18: next write is at the single-slot tail.
    sensors_event_t initial[19];
    for (int i = 0; i < 19; ++i) initial[i] = event(i);
    pthread_mutex_lock(&queue_mutex);
    queue.write(initial, 19, &queue_mutex, &data_available_cond);
    pthread_mutex_unlock(&queue_mutex);
    sensors_event_t output[64] = {};
    assert(ctx.poll(output, 18) == 18);
    for (int i = 0; i < 18; ++i) assert(output[i].timestamp == i);

    TaskContext task = {reinterpret_cast<sensors_poll_device_t*>(&legacy), &queue};
    schedule({20});
    std::thread writer([&] {
        try {
            writerTask(&task);
        } catch (const StopWriter&) {
            // Test-only clean shutdown, thrown by the fake poll outside locks.
        }
    });
    wait_until([&] {
        pthread_mutex_lock(&queue_mutex);
        bool pending = queue.getSize() == 20 && queue.getPendingSize() == 21;
        pthread_mutex_unlock(&queue_mutex);
        return pending;
    });
    // One staged event is waiting for space. Both flush barriers must follow
    // that event, not merely the 20 events currently readable in the ring.
    assert(ctx.flush(handle) == 0);
    assert(ctx.flush(handle) == 0);
    int expected = 18;
    int flushes = 0;
    while (flushes < 2) {
        int count = ctx.poll(output, 64);
        assert(count > 0);
        for (int i = 0; i < count; ++i) {
            if (output[i].type == SENSOR_TYPE_META_DATA) {
                assert(expected == 39);
                assert(output[i].sensor == 0);
                assert(output[i].meta_data.what == META_DATA_FLUSH_COMPLETE);
                assert(output[i].meta_data.sensor == handle);
                ++flushes;
            } else {
                assert(flushes == 0);
                assert(output[i].sensor == handle);
                assert(output[i].timestamp == expected++);
            }
        }
    }
    assert(expected == 39 && ctx.pending_flushes.empty());

    // Invalid returns do not change the ring, and the next valid pair is
    // delivered without a persistent tail stall.
    schedule({0, -EIO, SENSOR_EVENT_QUEUE_CAPACITY + 1, 2});
    assert(ctx.poll(output, 64) == 2);
    assert(output[0].timestamp == 39 && output[1].timestamp == 40);
    {
        std::lock_guard<std::mutex> lock(poll_mutex);
        assert(poll_calls == 5);
    }

    // Stress continuous producer/consumer wraparound with varying batch and
    // read sizes. Sequence numbers prove no losses, duplication or reordering.
    int total = 0;
    {
        std::lock_guard<std::mutex> lock(poll_mutex);
        const int batches[] = {2, 20, 3, 19, 1, 7};
        for (int i = 0; i < 6000; ++i) {
            int count = batches[i % 6];
            replies.push_back(count);
            total += count;
        }
        poll_ready.notify_all();
    }
    expected = 41;
    const int end = expected + total;
    while (expected < end) {
        int count = ctx.poll(output, 13);
        for (int i = 0; i < count; ++i) {
            assert(output[i].sensor == handle && output[i].timestamp == expected++);
        }
    }
    assert(expected == end);
    {
        std::lock_guard<std::mutex> lock(poll_mutex);
        assert(replies.empty());
        stop_writer = true;
        poll_ready.notify_all();
    }
    writer.join();

    // Empty-queue flush wakes a blocked framework poll.
    std::thread reader([&] {
        sensors_event_t result = {};
        assert(ctx.poll(&result, 1) == 1);
        assert(result.type == SENSOR_TYPE_META_DATA && result.meta_data.sensor == handle);
    });
    wait_until([] {
        pthread_mutex_lock(&queue_mutex);
        bool waiting = waiting_for_data;
        pthread_mutex_unlock(&queue_mutex);
        return waiting;
    });
    assert(ctx.flush(handle) == 0);
    reader.join();
    assert(ctx.activate(handle, 0) == 0 && ctx.flush(handle) == -EINVAL);
    assert(ctx.poll(output, 0) == -EINVAL);
    printf("PASS: adapter tail, invalid returns, backpressure, flush ordering/wakeup, "
           "legacy API isolation, %d ordered stress events\n", total);
}

int main() {
    alarm(30);
    for (int capacity : {1, 2, 3, 20}) test_empty_tail_and_backpressure(capacity);
    puts("PASS: empty tail and multi-wrap copying at capacities 1, 2, 3, 20");
    test_adapter();
    return 0;
}
