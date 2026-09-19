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

    PathManager::Init();
    Logger::Init();

    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &msg) {
        LogLevel level = LogLevel::INFO;
        switch (type) {
            case QtDebugMsg:    level = LogLevel::DEBUG; break;
            case QtInfoMsg:     level = LogLevel::INFO; break;
            case QtWarningMsg:  level = LogLevel::WARNING; break;
            case QtCriticalMsg:
            case QtFatalMsg:    level = LogLevel::ERROR; break;
        }
        std::string sourceInfo = "";
        if (context.file) {
            sourceInfo = " (" + std::string(context.file) + ":" + std::to_string(context.line) + ")";
        }
        Logger::Log(level, "[Qt] " + msg.toStdString() + sourceInfo);
    });

    QCoreApplication::setOrganizationName("VKAudioTeam");
    QCoreApplication::setApplicationName("VKAudioPlayer");

    QtWebView::initialize();

    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    QMap<QString, QString> envVars = EnvParser::Parse(".env");
    Logger::Log(LogLevel::INFO, "--- VK Audio Player Started ---");
    Logger::Log(LogLevel::INFO, "DB Path: " + PathManager::GetDbPath().toStdString());

    auto appCore = std::make_unique<ApplicationCore>(envVars);

    if (!appCore->Initialize()) {
        Logger::Log(LogLevel::ERROR, "Main: Failed to initialize ApplicationCore. Exiting.");
        return -1;
    }

    appCore->Start();

    int exitCode = app.exec();
    appCore.reset();
    Logger::Close();
    return exitCode;
}