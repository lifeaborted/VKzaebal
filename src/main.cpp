#include <QGuiApplication>
#include <QtWebView>
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include "utils/env/EnvParser.h"
#include "core/ApplicationCore.h"

#ifdef _WIN32
#include <windows.h>
#undef ERROR
#endif

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
#endif

    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QtWebView::initialize();

    QCoreApplication::setOrganizationName("VKAudioTeam");
    QCoreApplication::setApplicationName("VKAudioPlayer");

    PathManager::Init();
    Logger::Init();

    QMap<QString, QString> envVars = EnvParser::Parse(".env");
    Logger::Log(LogLevel::INFO, "--- VK Audio Player Started ---");
    Logger::Log(LogLevel::INFO, "DB Path: " + PathManager::GetDbPath().toStdString());

    // DI Контейнер берет на себя всю тяжелую работу
    ApplicationCore appCore(envVars);

    if (!appCore.Initialize()) {
        Logger::Log(LogLevel::ERROR, "Main: Failed to initialize ApplicationCore. Exiting.");
        return -1;
    }

    appCore.Start();

    int exitCode = app.exec();
    Logger::Close();
    return exitCode;
}