#include "AudioCaptureService.h"
#include "utils/path/PathManager.h"
#include "utils/logger/Logger.h"

#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

bool AudioCaptureService::RecordToFile(const std::string& filePath, int durationSeconds) {
#ifdef _WIN32
    mciSendStringA("open new type waveaudio alias rec", NULL, 0, NULL);
    mciSendStringA("record rec", NULL, 0, NULL);
    std::this_thread::sleep_for(std::chrono::seconds(durationSeconds > 0 ? durationSeconds : 7));

    std::string saveCmd = "save rec \"" + filePath + "\"";
    MCIERROR err = mciSendStringA(saveCmd.c_str(), NULL, 0, NULL);
    mciSendStringA("close rec", NULL, 0, NULL);
    return (err == 0);
#else
    Logger::Log(LogLevel::WARNING, "AudioCaptureService: Microphone recording is currently only implemented for Windows.");
    return false;
#endif
}

std::string AudioCaptureService::GetDefaultRecordPath() {
    return (PathManager::GetTempDir() + "/shazam_mic_record.wav").toStdString();
}
