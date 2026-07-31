#include "RingBuffer.h"
#include <algorithm>
#include <cstring>

RingBuffer::RingBuffer(size_t size) 
    : m_capacity(size), m_readPos(0), m_writePos(0), m_availableBytes(0) {
    m_buffer.resize(size);
}

size_t RingBuffer::GetAvailableRead() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_availableBytes;
}

size_t RingBuffer::GetAvailableWrite() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_capacity - m_availableBytes;
}

size_t RingBuffer::Write(const uint8_t* data, size_t sizeToWrite) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    size_t availableWrite = m_capacity - m_availableBytes;
    size_t actualWrite = std::min(sizeToWrite, availableWrite);
    
    if (actualWrite == 0) return 0; // Буфер переполнен

    // Пишем до конца буфера
    size_t firstPart = std::min(actualWrite, m_capacity - m_writePos);
    std::memcpy(&m_buffer[m_writePos], data, firstPart);
    
    // Если нужно, загибаемся в начало буфера
    if (firstPart < actualWrite) {
        std::memcpy(&m_buffer[0], data + firstPart, actualWrite - firstPart);
    }

    m_writePos = (m_writePos + actualWrite) % m_capacity;
    m_availableBytes += actualWrite;

    return actualWrite;
}

size_t RingBuffer::Read(uint8_t* data, size_t sizeToRead) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    size_t actualRead = std::min(sizeToRead, m_availableBytes);
    
    if (actualRead == 0) return 0; // Буфер пуст (сеть не успевает)

    size_t firstPart = std::min(actualRead, m_capacity - m_readPos);
    std::memcpy(data, &m_buffer[m_readPos], firstPart);
    
    if (firstPart < actualRead) {
        std::memcpy(data + firstPart, &m_buffer[0], actualRead - firstPart);
    }

    m_readPos = (m_readPos + actualRead) % m_capacity;
    m_availableBytes -= actualRead;

    return actualRead;
}