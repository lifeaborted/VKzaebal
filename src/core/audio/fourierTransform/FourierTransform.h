#pragma once

#include <vector>
#include <complex>

using Complex = std::complex<double>;

class FastFourierTransform {
public:
    // выделение памяти
    explicit FastFourierTransform(size_t n);

    void compute(std::vector<Complex>& data);

private:
    size_t m_size;
    std::vector<size_t> m_revTable;
    std::vector<Complex> m_twiddleFactors;

    // Методы для инициализации LUT-таблиц
    void initReverseTable();
    void initTwiddleFactors();
};