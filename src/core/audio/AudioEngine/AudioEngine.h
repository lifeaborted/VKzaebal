#pragma once
#include <string>
#include <memory>
#include "core/audio/miniaudio/miniaudio.h"
#include "utils/buffer/RingBuffer.h"

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    // Старый метод для локальных файлов (можно оставить для тестов)
    bool Initialize(const std::string& filePath);

    // Новый метод для стриминга (инициализирует буфер, но не запускает декодер сразу)
    bool InitializeStream(size_t bufferSize = 1024 * 1024 * 2); // По умолчанию 2 МБ

    // Метод, в который мы будем "заталкивать" скачанные байты
    void PushAudioData(const uint8_t* data, size_t size);

    void Play();
    void Pause();
    void SetVolume(float volume);
    void Shutdown();

    // Проверка, готов ли движок к воспроизведению (накопился ли пре-буфер)
    bool TryStartDecoder();

private:
    // Коллбэк для вывода звука на колонки
    static void DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

    // НОВЫЕ коллбэки для чтения напрямую из памяти
    static ma_result CustomRead(ma_decoder* pDecoder, void* pBufferOut, size_t bytesToRead, size_t* pBytesRead);
    static ma_result CustomSeek(ma_decoder* pDecoder, ma_int64 byteOffset, ma_seek_origin origin);

    ma_decoder m_decoder;
    ma_device m_device;
    bool m_isInitialized;
    bool m_isPlaying;
    bool m_isDecoderReady; // Флаг того, что заголовки прочитаны

    // Наш потокобезопасный буфер
    std::unique_ptr<RingBuffer> m_ringBuffer;
};