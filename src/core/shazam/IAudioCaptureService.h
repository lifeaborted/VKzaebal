#pragma once
#include <string>

class IAudioCaptureService {
public:
    virtual ~IAudioCaptureService() = default;
    virtual bool RecordToFile(const std::string& filePath, int durationSeconds) = 0;
    virtual std::string GetDefaultRecordPath() = 0;
};
