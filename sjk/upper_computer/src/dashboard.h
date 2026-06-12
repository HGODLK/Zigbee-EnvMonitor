#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <QWidget>
#include <QLabel>
#include "dataparser.h"

class Dashboard : public QWidget
{
    Q_OBJECT
public:
    explicit Dashboard(QWidget *parent = nullptr);
    void updateData(const SensorData &data);
    void setConnected(bool ok, const QString &host = "");

private:
    QLabel *m_tempVal;
    QLabel *m_humiVal;
    QLabel *m_lightVal;
    QLabel *m_statusIcon;
    QLabel *m_statusText;
};

#endif
