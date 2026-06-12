#include "datastore.h"
#include <QDir>
#include <QDateTime>

DataStore::DataStore(QObject *parent) : QObject(parent) {}

DataStore::~DataStore() { stopRecording(); }

void DataStore::startRecording(const QString &filePath)
{
    QString path = filePath;
    if (path.isEmpty()) {
        QString dir = QDir::currentPath() + "/data";
        QDir().mkpath(dir);
        path = dir + "/sensor_" +
               QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".csv";
    }

    m_file.setFileName(path);
    if (m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        m_stream.setDevice(&m_file);
        // Write header if file is empty
        if (m_file.size() == 0) {
            m_stream << "timestamp,temp,humi,light\n";
            m_stream.flush();
        }
        m_recording = true;
    }
}

void DataStore::stopRecording()
{
    if (m_recording) {
        m_stream.flush();
        m_file.close();
        m_recording = false;
    }
}

void DataStore::writeRow(const SensorData &data)
{
    if (!m_recording || !data.valid) return;
    m_stream << data.time.toString("yyyy-MM-dd HH:mm:ss") << ","
             << data.temp << ","
             << data.humi << ","
             << data.light << "\n";
    m_stream.flush();
}

bool DataStore::isRecording() const { return m_recording; }
