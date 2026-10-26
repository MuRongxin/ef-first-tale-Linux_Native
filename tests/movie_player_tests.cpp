#include "video/movie_player.hpp"
#include "audio/mixer.hpp"
#include "audio/output.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: ef_movie_player_tests <video>");
        using namespace ef;
        // Decode/scale and the wall-clock completion path (no audio device in this test).
        MoviePlayer movie(argv[1], 160, 120, nullptr);
        bool saw_frame = false;
        int updates = 0;
        while (!movie.finished() && updates < 1200) {
            movie.update(1.0 / 60.0);
            if (!movie.rgba().empty()) saw_frame = true;
            ++updates;
            // The decoder runs on a worker thread; give it real time when the test drives
            // the playback clock faster than wall time.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!saw_frame) throw std::runtime_error("movie produced no frame");
        if (!movie.finished()) throw std::runtime_error("movie did not finish on the silent clock");
        if (movie.width() != 160 || movie.height() != 120) throw std::runtime_error("movie destination size");
        // Skip is immediate and idempotent; a skipped decoder must not run further.
        MoviePlayer skipped(argv[1], 160, 120, nullptr);
        if (skipped.rgba().empty()) throw std::runtime_error("skip fixture produced no initial frame");
        skipped.skip();
        if (!skipped.finished()) throw std::runtime_error("skip did not finish the movie");
        skipped.update(1.0);
        if (!skipped.finished()) throw std::runtime_error("skipped movie resumed");
        // Audio-clock path and worker->ring streaming. The dummy device may consume faster
        // than real time; this verifies the pipeline and completion, not A/V wall timing.
        setenv("SDL_AUDIODRIVER", "dummy", 1);
        AudioMixer mixer;
        AudioOutput output(mixer, true);
        if (!output.active()) throw std::runtime_error("SDL dummy audio output is unavailable");
        {
            MoviePlayer audible(argv[1], 160, 120, &output);
            bool audible_frame = false;
            int audible_updates = 0;
            while (!audible.finished() && audible_updates < 6000) {
                audible.update(1.0 / 60.0);
                if (!audible.rgba().empty()) audible_frame = true;
                ++audible_updates;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!audible_frame) throw std::runtime_error("audio movie produced no frame");
            if (!audible.finished()) throw std::runtime_error("audio movie did not finish");
        }
        std::cout << "Movie player tests PASS: H.264 decode/scale, silent and audio clocks, completion and skip\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
