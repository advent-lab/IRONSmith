#include <cstddef>

// Dummy built-in kernel fixture used by the Kernels catalog panel.
void gemm_bf16(const unsigned short* a,
               const unsigned short* b,
               unsigned short* c,
               int m,
               int n,
               int k)
{
    if (!a || !b || !c || m <= 0 || n <= 0 || k <= 0)
        return;

    // Placeholder implementation for UI and assignment flow testing.
    for (int row = 0; row < m; ++row) {
        for (int col = 0; col < n; ++col) {
            const std::size_t idx = static_cast<std::size_t>(row) * static_cast<std::size_t>(n)
                                    + static_cast<std::size_t>(col);
            c[idx] = static_cast<unsigned short>(0);
        }
    }
}
