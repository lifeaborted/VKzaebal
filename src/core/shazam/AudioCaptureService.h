#pragma once
#include "IAudioCaptureService.h"

class AudioCaptureService : public IAudioCaptureService {
public:
    AudioCaptureService() = default;
    ~AudioCaptureService() override = default;

    bool RecordToFile(const std::string& filePath, int durationSeconds) override;
    std::string GetDefaultRecordPath() override;
};
