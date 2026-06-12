#ifndef DATAPARSER_H
#define DATAPARSER_H

#include <QObject>
#include <QJsonObject>
#include <QDateTime>

struct SensorData {
    double temp = 0.0;     // °C
    double humi = 0.0;     // %RH
    int    light = 0;      // %
    QDateTime time;
    bool valid = false;
};

class DataParser : public QObject
{
    Q_OBJECT
public:
    explicit DataParser(QObject *parent = nullptr);

    // WiFi: parse JSON from ESP8266 TCP
    static SensorData parseJson(const QByteArray &json);

    // Serial: parse "RCV-TEM1: 01 1D 00..." or "RCV-LIGHT2: 02 4B..."
    static SensorData parseSerialLine(const QString &line);

    // Merge partial data (e.g., TEM1 gives T+H, LIGHT2 gives Light)
    static void merge(SensorData &target, const SensorData &incoming);

signals:
    void newData(const SensorData &data);
};

#endif
