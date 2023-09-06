// File containing all the interrupt vectors used in the kernel.
#pragma once
#include <interrupts/interrupts.hpp>

namespace Interrupts::VectorMap {
// Vector used by the Programmable Interval Timer (PIT) when computing the
// frequency of the LAPIC timer.
static const Vector PitVector = Vector(32);

// Vector used by the LAPIC timer.
static const Vector LapicTimerVector = Vector(33);
}
