#pragma once
#include <cstdint>
#include <string>
namespace ef {
enum class ShakeKind { shift=1,zoom=2,rotation=3 };
struct ShakeFrame {
    ShakeKind kind{ShakeKind::shift};
    float x{},y{},crop{},angle{}; // design pixels, angle in degrees
    std::uint64_t sample{};
};
class Shake {
public:
    Shake(const std::string& mode,int amplitude,int duration,std::uint32_t seed=1);
    void update(double seconds);
    bool finished() const { return finished_; }
    ShakeFrame frame() const;
private:
    std::uint32_t random_at(std::uint64_t n) const;
    char mode_{};
    int amplitude_{},duration_{};
    std::uint32_t seed_{};
    double elapsed_{};
    bool sampled_{},finished_{};
};
}
