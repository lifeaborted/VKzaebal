#pragma once

#include <vector>
#include <complex>
#include <cstddef>

using Complex = std::complex<double>;

class FastFourierTransform {
public:
    // выделение памяти
    explicit FastFourierTransform(size_t n);

    void compute(std::vector<Complex>& data);
    [[nodiscard]] size_t GetSize() const noexcept { return m_size; }

private:
    size_t m_size{0};
    std::vector<size_t> m_revTable;
    std::vector<Complex> m_twiddleFactors;

    // Методы для инициализации LUT-таблиц
    void initReverseTable();
    void initTwiddleFactors();
};