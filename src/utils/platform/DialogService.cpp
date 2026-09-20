#include "DialogService.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#include <vector>
#else
#include <QFileDialog>
#endif

std::string DialogService::OpenAudioFileDialog() {
#ifdef _WIN32
    OPENFILENAMEW ofn;
    WCHAR szFile[MAX_PATH] = {0};

    ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Audio Files\0*.mp3;*.wav;*.aac;*.flac;*.ogg;*.m4a\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, ofn.lpstrFile, -1, NULL, 0, NULL, NULL);
        if (size_needed > 0) {
            std::vector<char> buffer(size_needed);
            WideCharToMultiByte(CP_UTF8, 0, ofn.lpstrFile, -1, &buffer[0], size_needed, NULL, NULL);
            return std::string(buffer.data());
        }
    }
    return "";
#else
    QString file = QFileDialog::getOpenFileName(nullptr, "Open Audio File", "", "Audio Files (*.mp3 *.wav *.aac *.flac *.ogg *.m4a);;All Files (*.*)");
    return file.toStdString();
#endif
}

std::string DialogService::ChooseFolderDialog(const std::string& title) {
#ifdef _WIN32
    std::string result;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog *pFileDialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileDialog)))) {
        DWORD dwOptions;
        if (SUCCEEDED(pFileDialog->GetOptions(&dwOptions))) {
            pFileDialog->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        }
        std::wstring wTitle = title.empty() ? L"Выберите папку для сохранения аудио" : std::wstring(title.begin(), title.end());
        pFileDialog->SetTitle(wTitle.c_str());
        if (SUCCEEDED(pFileDialog->Show(NULL))) {
            IShellItem *pItem = nullptr;
            if (SUCCEEDED(pFileDialog->GetResult(&pItem))) {
                PWSTR pszFilePath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                    int size = WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, NULL, 0, NULL, NULL);
                    if (size > 0) {
                        std::vector<char> buffer(size);
                        WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, buffer.data(), size, NULL, NULL);
                        result = std::string(buffer.data());
                    }
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileDialog->Release();
    }
    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
    return result;
#else
    QString dir = QFileDialog::getExistingDirectory(nullptr, QString::fromStdString(title.empty() ? "Выберите папку для сохранения аудио" : title));
    return dir.toStdString();
#endif
}
