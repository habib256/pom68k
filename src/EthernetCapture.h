// POM68K — VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
// Host-side observation of the Dayna link; never part of guest save state.
#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace pom68k {
class EthernetCapture {
public:
    static constexpr size_t kQueueFrames = 256, kMaxFrame = 1514;
    struct Status {
        bool accepting = false, busy = false;
        uint64_t submitted = 0, written = 0, dropped = 0;
        size_t queued = 0;
        std::string path, error;
    };
    ~EthernetCapture();
    // Host control only. Opening, writing and closing happen on the worker.
    // Existing PCAP/metadata files are refused rather than overwritten.
    bool start(std::string path, int64_t clockHz);
    void stop();                     // asynchronous drain; never joins the writer
    Status status() const;
    // One machine-thread producer. No allocation, file I/O or waiting for a
    // mutex: full/contended queues count a lost observation, not a guest loss.
    void observe(int64_t cycles, bool transmitted, const uint8_t* data, size_t size);
private:
    struct Frame {
        int64_t cycles = 0;
        bool transmitted = false;
        uint32_t size = 0;
        std::array<uint8_t,kMaxFrame> data{};
    };
    void write(std::string path, int64_t clockHz);
    void fail(std::string error);
    void finishObservations();
    mutable std::mutex mu_;
    std::condition_variable ready_;
    std::array<Frame,kQueueFrames> queue_{};
    size_t head_ = 0, count_ = 0;
    std::thread worker_;
    std::atomic<bool> accepting_{false}, busy_{false};
    std::atomic<uint64_t> submitted_{0}, written_{0}, dropped_{0};
    std::atomic<unsigned> observations_{0}; // finish accounting before closing metadata
    std::string path_, error_;
};
}
