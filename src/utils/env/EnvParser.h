#pragma once
#include <QString>
#include <QMap>

class EnvParser {
public:
    static QMap<QString, QString> Parse(const QString& filePath);
};