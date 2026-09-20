#pragma once
#include "IDialogService.h"

class DialogService : public IDialogService {
public:
    DialogService() = default;
    ~DialogService() override = default;

    std::string OpenAudioFileDialog() override;
    std::string ChooseFolderDialog(const std::string& title = "") override;
};
