#include "microphone.h"

#include <cstdio>

#include "failure.h"
#include "miniaudio.h"

struct Microphone::Device {
    ma_device device;
    // Set only for a device chosen by name; it must outlive the device.
    ma_context context;
    bool has_context = false;
    ~Device() {
        if (has_context) ma_context_uninit(&context);
    }
};

namespace {

Failure microphone_failure(const std::string & what, const std::string & device, ma_result result) {
    return Failure(speech_status_name(SPEECH_ERROR_IO), "",
                   "cannot " + what + " " + (device.empty() ? "the default microphone" : "the microphone " + device) + ": " + ma_result_description(result) +
                       "; check that one is connected and turned on");
}

/** The capture devices of a context; they belong to it. */
std::pair<ma_device_info *, ma_uint32> capture_devices(ma_context & context) {
    ma_device_info * devices = nullptr;
    ma_uint32 count = 0;
    if (const ma_result r = ma_context_get_devices(&context, nullptr, nullptr, &devices, &count); r != MA_SUCCESS) {
        throw microphone_failure("list", "", r);
    }
    return {devices, count};
}

}  // namespace

std::vector<MicrophoneInfo> list_microphones() {
    ma_context context;
    if (const ma_result r = ma_context_init(nullptr, 0, nullptr, &context); r != MA_SUCCESS) throw microphone_failure("list", "", r);
    std::vector<MicrophoneInfo> found;
    try {
        const auto [devices, count] = capture_devices(context);
        for (ma_uint32 i = 0; i < count; i++) found.push_back({devices[i].name, devices[i].isDefault != 0});
    } catch (...) {
        ma_context_uninit(&context);
        throw;
    }
    ma_context_uninit(&context);
    return found;
}

Microphone::Microphone(const std::string & device) : device_(new Device) {
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = 1;
    // The device's own rate, which the library resamples as it does a file's, rather than miniaudio's resampler.
    config.sampleRate = 0;
    config.pUserData = this;
    config.dataCallback = [](ma_device * device, void *, const void * input, ma_uint32 frames) {
        static_cast<Microphone *>(device->pUserData)->captured(static_cast<const float *>(input), frames);
    };
    ma_device_id id;
    if (!device.empty()) {
        if (const ma_result r = ma_context_init(nullptr, 0, nullptr, &device_->context); r != MA_SUCCESS) throw microphone_failure("open", device, r);
        device_->has_context = true;
        const auto [devices, count] = capture_devices(device_->context);
        std::string names;
        bool found = false;
        for (ma_uint32 i = 0; i < count && !found; i++) {
            if (device == devices[i].name) {
                id = devices[i].id;
                found = true;
            }
            names += (i ? ", " : "") + std::string(devices[i].name);
        }
        if (!found) throw Failure(speech_status_name(SPEECH_ERROR_IO), "", "no microphone is named " + device + "; the system offers: " + names);
        config.capture.pDeviceID = &id;
    }
    if (const ma_result r = ma_device_init(device_->has_context ? &device_->context : nullptr, &config, &device_->device); r != MA_SUCCESS) {
        throw microphone_failure("open", device, r);
    }
    rate_ = (int) device_->device.sampleRate;
    char name[MA_MAX_DEVICE_NAME_LENGTH + 1] = {0};
    ma_device_get_name(&device_->device, ma_device_type_capture, name, sizeof name, nullptr);
    name_ = name;
    if (const ma_result r = ma_device_start(&device_->device); r != MA_SUCCESS) {
        ma_device_uninit(&device_->device);
        throw microphone_failure("start", device, r);
    }
}

Microphone::~Microphone() {
    ma_device_uninit(&device_->device);
}

void Microphone::captured(const float * samples, size_t n) {
    std::lock_guard<std::mutex> lock(mutex_);
    captured_.insert(captured_.end(), samples, samples + n);
    arrived_.notify_all();
}

std::vector<float> Microphone::take(std::chrono::milliseconds wait) {
    std::vector<float> out;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        arrived_.wait_for(lock, wait, [&] { return !captured_.empty(); });
        out.swap(captured_);
    }
    // A microphone always hears some noise, so audio of nothing but zeros for seconds is one the system keeps from the
    // program, as macOS does without the terminal's permission to use the microphone.
    bool zeros = true;
    for (const float s : out) zeros = zeros && s == 0;
    silent_ = zeros ? silent_ + (double) out.size() / rate_ : 0;
    if (silent_ >= 3 && !warned_) {
        std::fprintf(stderr, "the microphone has given nothing but zeros for 3 s: check that it is not muted, and on macOS that the terminal "
                             "may use it (System Settings, Privacy & Security, Microphone)\n");
        warned_ = true;
    }
    return out;
}
