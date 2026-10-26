#include "video/movie_player.hpp"
#include <iostream>
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
        while (!movie.finished() && updates < 600) {
            movie.update(1.0 / 60.0);
            if (!movie.rgba().empty()) saw_frame = true;
            ++updates;
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
        std::cout << "Movie player tests PASS: H.264 decode/scale, silent clock completion and skip\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
