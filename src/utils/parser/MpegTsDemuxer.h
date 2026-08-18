#pragma once
#include <vector>
#include <cstdint>
#include <functional>

enum class AudioFormat { Unknown, AAC_ADTS, MP3 };

class MpegTsDemuxer {
public:
    using PayloadCallback = std::function<void(const uint8_t* payload, size_t payloadSize, AudioFormat format)>;

    explicit MpegTsDemuxer(PayloadCallback callback);

    void ProcessBytes(const uint8_t* data, size_t size);
    void Reset();

private:
    AudioFormat DetectAudioFormat(const uint8_t* data, size_t size);

    PayloadCallback m_callback;
    std::vector<uint8_t> m_buffer;
    uint16_t m_audioPid = 0x1FFF;
    size_t m_id3BytesToSkip = 0;
    AudioFormat m_format = AudioFormat::Unknown;
    
    bool m_isTsStreamDetermined = false;
    bool m_isTsStream = false;
};