
#include <chrono>
#include <cmath> // Required header

static inline unsigned long millis()
{
    auto now = std::chrono::steady_clock::now();
    return (unsigned long) std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}
