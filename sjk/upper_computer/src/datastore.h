#ifndef DATASTORE_H
#define DATASTORE_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include "dataparser.h"

class DataStore : public QObject
{
    Q_OBJECT
public:
    explicit DataStore(QObject *parent = nullptr);
    ~DataStore();
    void startRecording(const QString &filePath = QString());
    void stopRecording();
    void writeRow(const SensorData &data);
    bool isRecording() const;

private:
    QFile m_file;
    QTextStream m_stream;
    bool m_recording = false;
};

#endif
