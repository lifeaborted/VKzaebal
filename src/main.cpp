#include <QGuiApplication>
#include <QtWebView>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <iostream>
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include "utils/env/EnvParser.h"
#include "core/ApplicationCore.h"
#include "ui/UiFactory.h"

#ifdef _WIN32
#include <windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")
#undef ERROR
#if defined(_DEBUG)
#include <crtdbg.h>
#endif

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* pException) {
    char buf[256];
    snprintf(buf, sizeof(buf), "[CRASH] Unhandled Exception: Code=0x%08lX, Addr=0x%p",
             pException->ExceptionRecord->ExceptionCode,
             pException->ExceptionRecord->ExceptionAddress);
    Logger::Log(LogLevel::ERROR, buf);
    Logger::Close();

    HANDLE hFile = CreateFileA("crash.dmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = pException;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &mei, NULL, NULL);
        CloseHandle(hFile);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetUnhandledExceptionFilter(CrashHandler);
#endif
#if defined(_WIN32) && defined(_DEBUG)
    int dbgFlags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
    dbgFlags |= _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF;
    _CrtSetDbgFlag(dbgFlags);
#endif
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
    QCoreApplication::setApplicationVersion("2.0.0");

    QtWebView::initialize();

    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath());
    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath() + "/plugins");

    QCommandLineParser parser;
    parser.setApplicationDescription("VKAudioPlayer - High-performance audio player & aggregator");
    QCommandLineOption helpOption = parser.addHelpOption();
    QCommandLineOption versionOption = parser.addVersionOption();

    QCommandLineOption tuiOption(QStringList() << "tui", "Launch Modern Terminal UI (FTXUI) [Default]");
    parser.addOption(tuiOption);

    QCommandLineOption cliOption(QStringList() << "cli", "Launch Classic Full Console UI (UltimateRenderer with animated visualizer)");
    parser.addOption(cliOption);

    QCommandLineOption cliLightOption(QStringList() << "cli-light", "Launch Lightweight Minimal Console UI (Low-resource line-based CLI)");
    parser.addOption(cliLightOption);

    if (!parser.parse(app.arguments())) {
        std::cerr << parser.errorText().toStdString() << "\n";
        return 1;
    }

    if (parser.isSet(helpOption)) {
        std::cout << parser.helpText().toStdString() << "\n";
        return 0;
    }

    if (parser.isSet(versionOption)) {
        std::cout << "VKAudioPlayer version " << QCoreApplication::applicationVersion().toStdString() << "\n";
        return 0;
    }

    UiMode uiMode = UiMode::TUI;
    if (parser.isSet(cliLightOption)) {
        uiMode = UiMode::CLI_LIGHT;
        Logger::Log(LogLevel::INFO, "Main: Selected UI mode: CLI_LIGHT (--cli-light)");
    } else if (parser.isSet(cliOption)) {
        uiMode = UiMode::CLI;
        Logger::Log(LogLevel::INFO, "Main: Selected UI mode: CLI (--cli)");
    } else {
        uiMode = UiMode::TUI;
        Logger::Log(LogLevel::INFO, "Main: Selected UI mode: TUI (default / --tui)");
    }

    QMap<QString, QString> envVars = EnvParser::Parse(".env");
    Logger::Log(LogLevel::INFO, "--- VK Audio Player Started ---");
    Logger::Log(LogLevel::INFO, "DB Path: " + PathManager::GetDbPath().toStdString());

    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        Logger::Log(LogLevel::INFO, ">>> QCoreApplication::aboutToQuit received! <<<");
    });

    auto appCore = std::make_unique<ApplicationCore>(envVars, uiMode);
    appCore->SetUiFactory([uiMode](ApplicationCore& core) {
        return UiFactory::Create(uiMode, core);
    });

    if (!appCore->Initialize()) {
        Logger::Log(LogLevel::ERROR, "Main: Failed to initialize ApplicationCore. Exiting.");
        return -1;
    }

    appCore->Start();
    Logger::Log(LogLevel::INFO, "Main: appCore->Start() finished, entering app.exec()...");

    int exitCode = app.exec();
    Logger::Log(LogLevel::INFO, "Main: app.exec() returned with exitCode=" + std::to_string(exitCode));
    appCore.reset();
    Logger::Close();
    return exitCode;
}