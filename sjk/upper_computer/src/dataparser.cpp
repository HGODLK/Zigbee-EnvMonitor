#include "dataparser.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

DataParser::DataParser(QObject *parent) : QObject(parent) {}

SensorData DataParser::parseJson(const QByteArray &json)
{
    SensorData d;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError) return d;

    QJsonObject obj = doc.object();
    d.time = QDateTime::currentDateTime();
    d.valid = false;

    if (obj.contains("temp")) {
        d.temp = obj["temp"].toDouble();
        d.valid = true;
    }
    if (obj.contains("humi")) {
        d.humi = obj["humi"].toDouble();
        d.valid = true;
    }
    if (obj.contains("light")) {
        d.light = obj["light"].toInt();
        d.valid = true;
    }
    return d;
}

// Serial formats:
//   RCV-TEM1: 01 1D 00 1D 08 42 | T=29.8 H=29.0
//   RCV-LIGHT2: 02 4B 00 | Light=75%
SensorData DataParser::parseSerialLine(const QString &line)
{
    SensorData d;
    d.time = QDateTime::currentDateTime();
    d.valid = false;

    // Try TEM1 line
    static QRegularExpression tem1_re(R"(T=(\d+\.?\d*)\s*H=(\d+\.?\d*))");
    auto m = tem1_re.match(line);
    if (m.hasMatch()) {
        d.temp = m.captured(1).toDouble();
        d.humi = m.captured(2).toDouble();
        d.valid = true;
    }

    // Try LIGHT2 line
    static QRegularExpression light_re(R"(Light=(\d+)%?)");
    auto m2 = light_re.match(line);
    if (m2.hasMatch()) {
        d.light = m2.captured(1).toInt();
        d.valid = true;
    }

    return d;
}

void DataParser::merge(SensorData &target, const SensorData &incoming)
{
    if (incoming.temp != 0.0 || incoming.humi != 0.0) {
        target.temp = incoming.temp;
        target.humi = incoming.humi;
    }
    if (incoming.light != 0) {
        target.light = incoming.light;
    }
    if (incoming.valid)
        target.time = incoming.time;
    target.valid = target.valid || incoming.valid;
}
