#include <sattari_audio_core/SattariRealtime.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>

namespace
{
struct Frame
{
    std::uint64_t generation = 0;
    float left = 0.0f;
    float right = 0.0f;
    std::uint32_t checksum = 0;
};

std::uint32_t checksumFor (std::uint64_t generation)
{
    return static_cast<std::uint32_t> ((generation * 2654435761ULL) ^ (generation >> 32));
}
}

int main()
{
    sattari::rt::AtomicSnapshot<Frame> snapshot;
    std::atomic<bool> done { false };
    std::atomic<int> failures { 0 };

    std::thread writer ([&]
    {
        for (std::uint64_t generation = 1; generation <= 250000; ++generation)
        {
            Frame frame;
            frame.generation = generation;
            frame.left = static_cast<float> (generation);
            frame.right = -frame.left;
            frame.checksum = checksumFor (generation);
            snapshot.store (frame);
        }
        done.store (true, std::memory_order_release);
    });

    while (! done.load (std::memory_order_acquire))
    {
        const auto frame = snapshot.load();
        if (frame.generation == 0)
            continue;

        if (frame.checksum != checksumFor (frame.generation)
            || frame.right != -frame.left
            || frame.left != static_cast<float> (frame.generation))
        {
            failures.fetch_add (1, std::memory_order_relaxed);
            break;
        }
    }

    writer.join();

    const auto final = snapshot.load();
    if (final.generation != 250000 || final.checksum != checksumFor (final.generation))
        failures.fetch_add (1, std::memory_order_relaxed);

    if (failures.load (std::memory_order_relaxed) != 0)
    {
        std::fprintf (stderr, "AtomicSnapshot contract FAILED\n");
        return 1;
    }

    std::printf ("AtomicSnapshot contract ok\n");
    return 0;
}
