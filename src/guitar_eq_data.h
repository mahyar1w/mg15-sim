#pragma once
#include <array>
namespace amp::guitar {
// shape: shelf slope exponent (bass/treble) or bell width in octaves (middle).
struct Control { double cut, boost, center, shape, taper; };
inline constexpr std::array<Control,3> controls = {{
    {-14.0285657563, 10.2708657586, 308.762565395, 2.62523687029, 2.62542331334},
    {-11.0426497107, 3.69051548072, 709.032326105, 0.453945382648, 0.95924898752},
    {-24, 8.50554552871, 3261.14179703, 1.11222940389, 1.28019189329},
}};
}
