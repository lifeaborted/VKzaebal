#include "RingBuffer.h"
#include <algorithm>
#include <cstring>

RingBuffer::RingBuffer(size_t size) {
    Init(size);
}

RingBuffer::RingBuffer() = default;

void RingBuffer::Init(size_t size) {
    m_capacity = size + 1;
    m_buffer.resize(m_capacity);
    m_readPos.store(0, std::memory_order_relaxed);
    m_writePos.store(0, std::memory_order_relaxed);
}

void RingBuffer::Clear() {
    m_readPos.store(0, std::memory_order_relaxed);
    m_writePos.store(0, std::memory_order_relaxed);
}

size_t RingBuffer::GetAvailableRead() const {
    size_t w = m_writePos.load(std::memory_order_acquire);
    size_t r = m_readPos.load(std::memory_order_relaxed);
    if (w >= r) {
        return w - r;
    }
    return m_capacity - r + w;
}

size_t RingBuffer::GetAvailableWrite() const {
    size_t w = m_writePos.load(std::memory_order_relaxed);
    size_t r = m_readPos.load(std::memory_order_acquire);
    if (w >= r) {
        return m_capacity - 1 - (w - r);
    }
    return r - w - 1;
}

size_t RingBuffer::Write(const uint8_t* data, size_t sizeToWrite) {
    size_t r = m_readPos.load(std::memory_order_acquire);
    size_t w = m_writePos.load(std::memory_order_relaxed);

    size_t availableWrite;
    if (w >= r) {
        availableWrite = m_capacity - 1 - (w - r);
    } else {
        availableWrite = r - w - 1;
    }

    size_t actualWrite = std::min(sizeToWrite, availableWrite);
    if (actualWrite == 0) return 0; // Буфер переполнен

    size_t firstPart = std::min(actualWrite, m_capacity - w);
    std::memcpy(&m_buffer[w], data, firstPart);

    if (firstPart < actualWrite) {
        std::memcpy(&m_buffer[0], data + firstPart, actualWrite - firstPart);
    }

    // Сообщаем потоку чтения, что данные готовы
    m_writePos.store((w + actualWrite) % m_capacity, std::memory_order_release);

    return actualWrite;
}

size_t RingBuffer::Read(uint8_t* data, size_t sizeToRead) {
    size_t w = m_writePos.load(std::memory_order_acquire);
    size_t r = m_readPos.load(std::memory_order_relaxed);

    size_t availableRead;
    if (w >= r) {
        availableRead = w - r;
    } else {
        availableRead = m_capacity - r + w;
    }

    size_t actualRead = std::min(sizeToRead, availableRead);
    if (actualRead == 0) return 0; // Буфер пуст

    // Читаем первую часть
    size_t firstPart = std::min(actualRead, m_capacity - r);
    std::memcpy(data, &m_buffer[r], firstPart);

    // Дочитываем с начала буфера
    if (firstPart < actualRead) {
        std::memcpy(data + firstPart, &m_buffer[0], actualRead - firstPart);
    }

    // Сообщаем потоку записи, что место освободилось
    m_readPos.store((r + actualRead) % m_capacity, std::memory_order_release);

    return actualRead;
}