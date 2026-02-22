#include <algorithm>

// Dummy built-in kernel fixture used by the Kernels catalog panel.
void eltwise_add_i8(const signed char* lhs,
                    const signed char* rhs,
                    signed char* out,
                    int size)
{
    if (!lhs || !rhs || !out || size <= 0)
        return;

    for (int i = 0; i < size; ++i) {
        const int sum = static_cast<int>(lhs[i]) + static_cast<int>(rhs[i]);
        out[i] = static_cast<signed char>(std::clamp(sum, -128, 127));
    }
}
