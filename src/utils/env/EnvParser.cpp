#include "EnvParser.h"
#include "utils/logger/Logger.h"
#include <QFile>
#include <QTextStream>

QMap<QString, QString> EnvParser::Parse(const QString& filePath) {
    QMap<QString, QString> env;
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith("#")) continue;
            int idx = line.indexOf('=');
            if (idx != -1) {
                QString key = line.left(idx).trimmed();
                QString value = line.mid(idx + 1).trimmed();
                if (value.startsWith('"') && value.endsWith('"')) {
                    value = value.mid(1, value.length() - 2);
                }
                env[key] = value;
            }
        }
    } else {
        Logger::Log(LogLevel::WARNING, "EnvParser: .env file not found at " + filePath.toStdString());
    }
    return env;
}