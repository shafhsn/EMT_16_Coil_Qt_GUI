#include "Worker.h"
#include <QThread>
#include <QDebug>

    Worker::Worker(QObject *parent)
    : QObject(parent)
{
}

void Worker::setQueue(QQueue<QString> *queue, QMutex *mutex)
{
    // Store the pointers for use in processQueue()
    // (Assume queue and mutex will remain valid during processing)
    this->queue = queue;
    this->mutex = mutex;
}

void Worker::processQueue()
{
    // Process in a loop (you might use a condition variable in a real app)
    while (true) {
        mutex->lock();
        if (!queue->isEmpty()) {
            // Retrieve the next element from the queue
            QString item = queue->dequeue();
            mutex->unlock();

            // Process the item (simulate some processing)
            QString result = "Processed: " + item;
            emit processed(result);
        } else {
            mutex->unlock();
            // Sleep briefly to avoid busy waiting
            QThread::msleep(100);
        }
    }
}
