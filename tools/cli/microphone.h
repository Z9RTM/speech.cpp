#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// The system's microphones, through miniaudio, for speech asr --live, speech talk --live and speech mics.

/** A capture device the system offers, by the name Microphone takes. */
struct MicrophoneInfo {
    std::string name;
    bool is_default = false;
};

/** The capture devices the system offers; a failure to ask throws a Failure of io. */
std::vector<MicrophoneInfo> list_microphones();

/** A capture device, started, giving mono 32-bit float samples at the device's own rate. */
class Microphone {
public:
    /**
     * Opens and starts the capture device of this exact name, the first of several that share it, or the default one
     * when empty; no such device, or one that does not open or start, throws a Failure of io.
     */
    explicit Microphone(const std::string & device = "");
    /** Stops the device. */
    ~Microphone();
    Microphone(const Microphone &) = delete;
    Microphone & operator=(const Microphone &) = delete;

    int rate() const { return rate_; }
    const std::string & name() const { return name_; }
    /**
     * The samples captured since the last call, waiting up to `wait` while there are none. Once they have been nothing
     * but zeros for 3 s, it says on stderr, once, what keeps the microphone from the program.
     */
    std::vector<float> take(std::chrono::milliseconds wait);

private:
    struct Device;

    void captured(const float * samples, size_t n);

    std::unique_ptr<Device> device_;
    int rate_ = 0;
    std::string name_;
    std::mutex mutex_;
    std::condition_variable arrived_;
    std::vector<float> captured_;
    double silent_ = 0;
    bool warned_ = false;
};
