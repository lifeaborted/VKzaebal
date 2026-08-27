#pragma once
#include <vector>
#include <atomic>
#include <cstdint>

class RingBuffer {
public:
    explicit RingBuffer(size_t size);
    RingBuffer();

    // передача данных (вызывается фоновым потоком скачивания)
    size_t Write(const uint8_t* data, size_t sizeToWrite);

    // сбор данных (вызывается аудио-потоком)
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
    size_t m_capacity = 0;

    // Lock-free счетчики
    alignas(64) std::atomic<size_t> m_writePos{0};
    alignas(64) std::atomic<size_t> m_readPos{0};
};