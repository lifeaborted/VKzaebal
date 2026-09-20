#pragma once
#include <string>

class IDialogService {
public:
    virtual ~IDialogService() = default;
    virtual std::string OpenAudioFileDialog() = 0;
    virtual std::string ChooseFolderDialog(const std::string& title = "") = 0;
};
