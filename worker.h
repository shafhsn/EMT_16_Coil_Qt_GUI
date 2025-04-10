#ifndef WORKER_H
#define WORKER_H

#include <QObject>
#include <QQueue>
#include <QMutex>
#include <QString>
#include <QThread>

class Worker : public QObject
{
    Q_OBJECT
public:
    explicit Worker(QObject *parent = nullptr);

    void setQueue(QQueue<QString> *queue, QMutex *mutex);

public slots:
    void processQueue();

signals:
    void processed(const QString &result);
};

#endif // WORKER_H
