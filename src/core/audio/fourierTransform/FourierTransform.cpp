#include "FourierTransform.h"
#include <cmath>
#include <algorithm>
#include <cassert>
#include <bit>

const double PI = 3.14159265358979323846;

FastFourierTransform::FastFourierTransform(size_t n) {
    if (n < 2) {
        m_size = 2;
    } else if (!std::has_single_bit(n)) {
        m_size = std::bit_ceil(n);
    } else {
        m_size = n;
    }
    initReverseTable();
    initTwiddleFactors();
}

void FastFourierTransform::initReverseTable() {
    if (m_size == 0) return;
    m_revTable.resize(m_size);
    size_t log_n = static_cast<size_t>(std::log2(m_size));
    
    for (size_t i = 0; i < m_size; ++i) {
        size_t rev = 0;
        for (size_t j = 0; j < log_n; ++j) {
            if (i & (1ULL << j)) {
                rev |= (1ULL << (log_n - 1 - j));
            }
        }
        m_revTable[i] = rev;
    }
}

void FastFourierTransform::initTwiddleFactors() {
    if (m_size == 0) return;
    m_twiddleFactors.resize(m_size / 2);
    for (size_t i = 0; i < m_size / 2; ++i) {
        double angle = -2.0 * PI * static_cast<double>(i) / static_cast<double>(m_size);
        m_twiddleFactors[i] = Complex(std::cos(angle), std::sin(angle));
    }
}

void FastFourierTransform::compute(std::vector<Complex>& data) {
    if (m_size == 0) return;

    assert(data.size() >= m_size && "FastFourierTransform::compute: data size is smaller than FFT size");

    if (data.size() < m_size) {
        data.resize(m_size, Complex(0.0, 0.0));
    }

    // 1. Битово-инверсная перестановка элементов
    for (size_t i = 0; i < m_size; ++i) {
        if (i < m_revTable[i]) {
            std::swap(data[i], data[m_revTable[i]]);
        }
    }

    // 2. Итеративный расчет
    for (size_t len = 2; len <= m_size; len <<= 1) {
        size_t half_len = len >> 1;
        size_t step = m_size / len;

        for (size_t i = 0; i < m_size; i += len) {
            for (size_t j = 0; j < half_len; ++j) {
                Complex w = m_twiddleFactors[j * step];
                Complex u = data[i + j];
                Complex v = data[i + j + half_len] * w;

                data[i + j] = u + v;
                data[i + j + half_len] = u - v;
            }
        }
    }
}