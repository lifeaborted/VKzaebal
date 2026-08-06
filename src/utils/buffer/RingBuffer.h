#pragma once
#include <vector>
#include <mutex>
#include <cstdint>

class RingBuffer {
public:
    explicit RingBuffer(size_t size);
    // пустой конструктор
    RingBuffer();
    
    // передача данных
    size_t Write(const uint8_t* data, size_t sizeToWrite);
    
    // сбор данных
    size_t Read(uint8_t* data, size_t sizeToRead);
    
    // Узнать, сколько байт доступно для чтения
    size_t GetAvailableRead() const;
    
    // Узнать, сколько свободного места осталось
    size_t GetAvailableWrite() const;

    // методы инициализации
    void Init(size_t size);

    // методы очистки
    void Clear();

private:
    std::vector<uint8_t> m_buffer;
    size_t m_capacity;
    size_t m_readPos;
    size_t m_writePos;
    size_t m_availableBytes;
    
    mutable std::mutex m_mutex;
};