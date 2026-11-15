#pragma once

// Row-parallel loops for the data-parallel stages of a photo-sized Frame
// (the Lab conversion, the blur and the FH edge weights). Every item is
// computed by the same code whichever thread takes it, and nothing is summed
// across items in floats, so the output is the same with any number of
// threads, in any order.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace mosaic::core {

// Frames smaller than this run on the calling thread alone: at 320x240 a
// thread would cost more than it saves, and the live preview stays as it was.
constexpr size_t parallelPixels = size_t(1) << 20;

// At most this many threads, the caller included (the Pixel 8a has one X3
// and four A715 cores).
constexpr int maxThreads = 5;

// The number of threads for a Frame of `pixels` pixels.
inline int threadsFor(size_t pixels) {
    if (pixels < parallelPixels) return 1;
    const int hardware = int(std::thread::hardware_concurrency());
    return std::clamp(hardware, 1, maxThreads);
}

// Calls work(from, to, slot) for consecutive chunks of `grain` items that
// cover 0..count-1, on `threads` threads (the caller is one of them); `slot`
// (0..threads-1) names the thread, for per-thread scratch. Chunks are handed
// out as threads ask for them, so a slow core takes fewer. If a thread can't
// be started, the others do its share. With one thread, it is one call over
// all the items.
template <class Work> void parallelFor(int count, int grain, int threads, const Work& work) {
    if (threads <= 1) {
        // One call over everything: the chunked loop measurably slowed the
        // Lab of a 320x240 Frame.
        if (count > 0) work(0, count, 0);
        return;
    }
    grain = std::max(1, grain);
    const int chunks = (count + grain - 1) / grain;
    threads = std::clamp(threads, 1, std::max(1, chunks));
    std::atomic<int> next{0};
    auto run = [&](int slot) {
        for (int chunk = next.fetch_add(1); chunk < chunks; chunk = next.fetch_add(1)) {
            const int from = chunk * grain;
            work(from, std::min(count, from + grain), slot);
        }
    };
    std::vector<std::thread> helpers;
    for (int slot = 1; slot < threads; slot++) {
        try {
            helpers.emplace_back(run, slot);
        } catch (...) {
            break;
        }
    }
    run(0);
    for (std::thread& helper : helpers) helper.join();
}

} // namespace mosaic::core
