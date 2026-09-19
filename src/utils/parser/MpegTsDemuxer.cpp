#include "MpegTsDemuxer.h"
#include "Id3Utils.h"
#include "utils/logger/Logger.h"
#include <algorithm>

MpegTsDemuxer::MpegTsDemuxer(PayloadCallback callback) : m_callback(callback) {}

void MpegTsDemuxer::Reset() {
    m_buffer.clear();
    m_audioPid = 0x1FFF;
    m_id3BytesToSkip = 0;
    m_format = AudioFormat::Unknown;
    m_isTsStreamDetermined = false;
    m_isTsStream = false;
}

AudioFormat MpegTsDemuxer::DetectAudioFormat(const uint8_t* data, size_t size) {
    if (size < 2 || data[0] != 0xFF) return AudioFormat::Unknown;
    uint8_t b1 = data[1];
    // ADTS (AAC)
    if ((b1 & 0xF0) == 0xF0) {
        if (((b1 >> 1) & 0x03) == 0x00) return AudioFormat::AAC_ADTS;
    }
    // MP3
    if ((b1 & 0xE0) == 0xE0) {
        if (((b1 >> 3) & 0x03) != 0x01 && ((b1 >> 1) & 0x03) != 0x00) return AudioFormat::MP3;
    }
    return AudioFormat::Unknown;
}

void MpegTsDemuxer::ProcessBytes(const uint8_t* data, size_t size) {
    if (!data || size == 0) return;

    if (!m_isTsStreamDetermined) {
        m_isTsStream = (data[0] == 0x47);
        m_isTsStreamDetermined = true;
    }

    if (!m_isTsStream) {
        size_t id3Size = Id3Utils::ParseHeaderTotalSize(data, size);
        if (id3Size > 0) {
            m_id3BytesToSkip = id3Size;
        }

        if (m_id3BytesToSkip > 0) {
            size_t toSkip = std::min(m_id3BytesToSkip, size);
            data += toSkip;
            size -= toSkip;
            m_id3BytesToSkip -= toSkip;
            if (size == 0) return;
        }

        if (m_format == AudioFormat::Unknown) m_format = DetectAudioFormat(data, size);
        m_callback(data, size, m_format);
        return;
    }

    m_buffer.insert(m_buffer.end(), data, data + size);
    size_t bytesConsumed = 0;

    while (m_buffer.size() - bytesConsumed >= 188) {
        const uint8_t* tsPacket = m_buffer.data() + bytesConsumed;

        if (tsPacket[0] != 0x47) {
            auto startIt = m_buffer.begin() + bytesConsumed;
            auto it = std::find(startIt, m_buffer.end(), 0x47);
            bytesConsumed = std::distance(m_buffer.begin(), it);
            continue;
        }

        uint16_t pid = ((tsPacket[1] & 0x1F) << 8) | tsPacket[2];
        uint8_t pusi = (tsPacket[1] & 0x40) >> 6;
        uint8_t afc  = (tsPacket[3] & 0x30) >> 4;

        size_t payloadOffset = 4;
        if (afc == 2 || afc == 3) payloadOffset += 1 + tsPacket[4];

        if ((afc == 1 || afc == 3) && payloadOffset < 188) {
            size_t payloadSize = 188 - payloadOffset;
            const uint8_t* payload = tsPacket + payloadOffset;

            if (pusi == 1 && payloadSize >= 9 && payload[0] == 0x00 && payload[1] == 0x00 && payload[2] == 0x01) {
                uint8_t streamId = payload[3];

                if (m_audioPid == 0x1FFF && streamId >= 0xC0 && streamId <= 0xDF) {
                    m_audioPid = pid;
                    Logger::Log(LogLevel::INFO, "Demuxer: Locked to AUDIO PID -> " + std::to_string(pid));
                }

                if (pid == m_audioPid) {
                    uint8_t pesHeaderLen = payload[8];
                    size_t pesTotalOffset = 9 + pesHeaderLen;
                    if (pesTotalOffset < payloadSize) {
                        payload += pesTotalOffset;
                        payloadSize -= pesTotalOffset;
                    } else {
                        payloadSize = 0;
                    }

                    size_t id3Size = Id3Utils::ParseHeaderTotalSize(payload, payloadSize);
                    if (id3Size > 0) {
                        m_id3BytesToSkip = id3Size;
                    }
                }
            }

            if (pid == m_audioPid && m_id3BytesToSkip > 0 && payloadSize > 0) {
                size_t toSkip = std::min(m_id3BytesToSkip, payloadSize);
                payload += toSkip;
                payloadSize -= toSkip;
                m_id3BytesToSkip -= toSkip;
            }

            if (payloadSize > 0 && pid == m_audioPid) {
                if (m_format == AudioFormat::Unknown) m_format = DetectAudioFormat(payload, payloadSize);
                m_callback(payload, payloadSize, m_format);
            }
        }
        bytesConsumed += 188;
    }

    if (bytesConsumed > 0) {
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + bytesConsumed);
    }
}