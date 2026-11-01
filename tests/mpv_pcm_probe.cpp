// Offline libmpv PCM capture for decoder-adapter experiments. GPL-3.0-or-later.
// Writes only the requested private output file; never opens an audio device.
#include <mpv/client.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    if (argc != 5 && argc != 8) return 2; // input, output, rate, channels[, first, end, decodeStartSeconds]
    mpv_handle* player = mpv_create();
    if (!player) return 3;
    int result = 1;
    try {
        const auto option = [&](const char* name, const char* value) {
            const int error = mpv_set_option_string(player, name, value);
            if (error < 0) throw std::runtime_error(std::string(name) + ": " + mpv_error_string(error));
        };
        option("config", "no");
        option("load-scripts", "no");
        option("vid", "no");
        option("ao", "pcm");
        option("ao-pcm-file", argv[2]);
        option("ao-pcm-waveheader", "no");
        option("audio-format", "s16");
        option("audio-samplerate", argv[3]);
        option("audio-channels", std::string(argv[4]) == "1" ? "mono" : "stereo");
        option("volume", "100");
        option("replaygain", "no");
        option("audio-display", "no");
        option("ytdl", "no");
        option("terminal", "no");
        if (argc == 8) {
            const auto number = [](const char* value) {
                const std::string text(value);
                if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
                    throw std::runtime_error("Invalid sample boundary");
                return text;
            };
            const auto filter = "lavfi=[atrim=start_pts=" + number(argv[5]) +
                                ":end_pts=" + number(argv[6]) + "]";
            option("af", filter.c_str());
            option("start", argv[7]);
            option("hr-seek", "yes");
            option("hr-seek-demuxer-offset", "0.25");
        }
        const int initialized = mpv_initialize(player);
        if (initialized < 0) throw std::runtime_error(mpv_error_string(initialized));
        for (const auto* name : {"mpv-version", "ffmpeg-version"}) {
            char* value = mpv_get_property_string(player, name);
            if (!value) throw std::runtime_error(std::string("Missing runtime identity: ") + name);
            std::cout << name << '=' << value << '\n';
            mpv_free(value);
        }
        const char* command[] = {"loadfile", argv[1], nullptr};
        const int loaded = mpv_command(player, command);
        if (loaded < 0) throw std::runtime_error(mpv_error_string(loaded));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        for (;;) {
            if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("PCM capture timed out");
            mpv_event* event = mpv_wait_event(player, 0.1);
            if (event->event_id == MPV_EVENT_END_FILE) {
                const auto* end = static_cast<const mpv_event_end_file*>(event->data);
                if (!end || end->reason != MPV_END_FILE_REASON_EOF || end->error < 0)
                    throw std::runtime_error("PCM capture did not reach natural EOF");
                result = 0;
                break;
            }
            if (event->event_id == MPV_EVENT_SHUTDOWN) throw std::runtime_error("Unexpected shutdown");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
    mpv_terminate_destroy(player);
    return result;
}
