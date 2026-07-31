#pragma once
#include <string>
#include <memory>
#include <functional>
#include "core/audio/miniaudio/miniaudio.h"
#include "utils/buffer/RingBuffer.h"

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool Initialize(const std::string& filePath);
    bool InitializeStream(size_t bufferSize = 1024 * 1024 * 2); // метод для стриминга (инициализирует буфер, но не запускает декодер сразу, по умолчанию 2 МБ)
    bool TryStartDecoder(); // Проверка, заполнения пре-буфера)

    void PushAudioData(const uint8_t* data, size_t size); // Метод, в который мы будем "заталкивать" скачанные байты
    void Play(); // метод воспроизведения аудио
    void Pause(); // метод паузы аудио
    void SetVolume(float volume); // метод изменения громкости
    void Shutdown(); // метод отключения воспроизведения
    void MarkStreamFinished(); // Метод проверки скачан ли файл

    std::function<void()> OnBufferFull;
    std::function<void()> OnBufferNeedsData;
    std::function<void()> OnTrackFinished; // Коллбэк для переключения трека

private:
    // Коллбэк для вывода звука
    static void DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

    // коллбэки для чтения напрямую из памяти
    static ma_result CustomRead(ma_decoder* pDecoder, void* pBufferOut, size_t bytesToRead, size_t* pBytesRead);
    static ma_result CustomSeek(ma_decoder* pDecoder, ma_int64 byteOffset, ma_seek_origin origin);

    ma_decoder m_decoder;
    ma_device m_device;

    bool m_isInitialized; // флаг инициализации проигрывателя
    bool m_isPlaying; // фалг проигрывания аудио
    bool m_isDecoderReady; // Флаг того, что заголовки прочитаны
    bool m_isNetworkPaused = false; // флаг защиты от спама
    bool m_isStreamFinished = false; // Флаг полного скачивания

    std::unique_ptr<RingBuffer> m_ringBuffer; // потокобезопасный буфер
};