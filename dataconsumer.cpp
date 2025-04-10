#include "dataconsumer.h"
#include <QMutexLocker>
#include <QDebug>
#include <algorithm>
#include <QThread>

/**
 * @brief DataConsumer::DataConsumer
 * Or dataConsumerThread in report,
**/
DataConsumer::DataConsumer(SharedBuffer *sharedBuffer, QObject *parent)
    : QObject{parent}
    , m_sharedBuffer(sharedBuffer)
    , m_stop(false)
{
}

/**
 * @brief DataConsumer::stop
 * Sets m_stop flag to = TRUE
 * Stops this thread, to exit the main app
 */
void DataConsumer::stop()
{
    QMutexLocker locker(&m_sharedBuffer->mutex);
    m_stop = true;
    qDebug()<<"Trying to STOP consumer thread from within";
    m_sharedBuffer->dataAvailable.wakeAll();
}

/**
 * @brief DataConsumer::processBuffers
 * Processes data that is sent by the other worked thread (processingDataThread).
 * Builds global2DArray, which holds the final data format which can be saved.
 * Passes global2DArray back to the main thread for saving.
 */
void DataConsumer::processBuffers()
{
    //if m_stop = TRUE, stop the thread
    while(!m_stop){
        if (QThread::currentThread()->isInterruptionRequested()){
            qDebug() << "STOPPING consumer thread";
            break;
        }

        //otherwise resize buffers
        {
            //locks mutex
            QMutexLocker locker(&m_sharedBuffer -> mutex);
            if (m_stop)
                break;
            //if buffer is not filled to 480 size, do not read yet
            while (m_sharedBuffer->bufferFinalFrequency.size() < chunkSize ||
                   m_sharedBuffer->bufferDecimated1.size() < chunkSize ||
                   m_sharedBuffer->bufferDecimated2.size() < chunkSize ||
                   m_sharedBuffer->bufferFourthArrayDivided.size() < chunkSize ||
                   m_sharedBuffer->bufferSixthArrayDivided.size() < chunkSize)
            //if buffer not full, put thread on waiting for call
            {
                m_sharedBuffer->dataAvailable.wait(&m_sharedBuffer->mutex, 100);
                if (QThread::currentThread()->isInterruptionRequested())
                    break;
            }
            if (m_stop)
                break;
        }

        //for data storage from queued containers
        QList<qint64> freqBuffer;
        QList<qint32> decimated1Buffer;
        QList<qint32> decimated2Buffer;
        QList<double> fourthArrayBuffer;
        QList<double> sixthArrayBuffer;

        //fill in each buffer with corresponding data
        {
            //locks mutex for thread-safe communication
            QMutexLocker locker(&m_sharedBuffer->mutex);
            for (int i = 0; i < chunkSize; ++i) {
                if (!m_sharedBuffer->bufferFinalFrequency.isEmpty())
                    freqBuffer.append(m_sharedBuffer->bufferFinalFrequency.dequeue());              //Actual Frequency
                if (!m_sharedBuffer->bufferDecimated1.isEmpty())
                    decimated1Buffer.append(m_sharedBuffer->bufferDecimated1.dequeue());            //Sensing Coil
                if (!m_sharedBuffer->bufferDecimated2.isEmpty())
                    decimated2Buffer.append(m_sharedBuffer->bufferDecimated2.dequeue());            //Excitation Coil
                if (!m_sharedBuffer->bufferFourthArrayDivided.isEmpty())
                    fourthArrayBuffer.append(m_sharedBuffer->bufferFourthArrayDivided.dequeue());   //Real Data
                if (!m_sharedBuffer->bufferSixthArrayDivided.isEmpty())
                    sixthArrayBuffer.append(m_sharedBuffer->bufferSixthArrayDivided.dequeue());     //Imaginary Data
            }
        }

        //if the sync button was pressed, then run following loop
        if(m_syncEnabled.loadAcquire()){
            if (!decimated1Buffer.isEmpty()) {
                int X = decimated1Buffer.at(0);
                bool unequalFlag = false;
                int iterationIndex = 0;
                int limit = qMin(5, decimated1Buffer.size());

                //check first 5 elements of sensing coils
                //use to reorder data correctly
                for (iterationIndex = 0; iterationIndex < limit; iterationIndex++) {
                    int Y = decimated1Buffer.at(iterationIndex);
                    if (X != Y) {
                        unequalFlag = true;
                        break;
                    }
                    X = Y;
                }

                //autosync must be [0,4];
                iterationIndex = qBound(0, iterationIndex, 4);
                if (unequalFlag) {
                    if (iterationIndex == 4)
                        autosync = 0;
                    else
                        autosync = iterationIndex;
                }
            }
        } m_syncEnabled = false;

        //update 'Auto Sync' display on GUI
        emit autoSyncUpdated(autosync);

        //rotate elements of each array by autosync value, pushing the last entries first, and first entries down
        if (autosync > 0) {
            if (!freqBuffer.isEmpty()) {
                int k = autosync % freqBuffer.size();
                std::rotate(freqBuffer.begin(), freqBuffer.end() - k, freqBuffer.end());
            }
            if (!decimated1Buffer.isEmpty()) {
                int k = autosync % decimated1Buffer.size();
                std::rotate(decimated1Buffer.begin(), decimated1Buffer.end() - k, decimated1Buffer.end());
            }
            if (!decimated2Buffer.isEmpty()) {
                int k = autosync % decimated2Buffer.size();
                std::rotate(decimated2Buffer.begin(), decimated2Buffer.end() - k, decimated2Buffer.end());
            }
            if (!fourthArrayBuffer.isEmpty()) {
                int k = autosync % fourthArrayBuffer.size();
                std::rotate(fourthArrayBuffer.begin(), fourthArrayBuffer.end() - k, fourthArrayBuffer.end());
            }
            if (!sixthArrayBuffer.isEmpty()) {
                int k = autosync % sixthArrayBuffer.size();
                std::rotate(sixthArrayBuffer.begin(), sixthArrayBuffer.end() - k, sixthArrayBuffer.end());
            }
        }

        //containers to store only the required number of elements for saving
        QList<qint64> freqBufferDecimated;
        QList<qint32> decimated1BufferDecimated;
        QList<qint32> decimated2BufferDecimated;
        QList<double> fourthArrayBufferDecimated;
        QList<double> sixthArrayBufferDecimated;

        //start at the 4th element, then save only every 4th element of each array
        //discard the other elements
        for (int i = 3; i < freqBuffer.size(); i += 4) {
            freqBufferDecimated.append(freqBuffer.at(i));
        }
        for (int i = 3; i < decimated1Buffer.size(); i += 4) {
            decimated1BufferDecimated.append(decimated1Buffer.at(i));
        }
        for (int i = 3; i < decimated2Buffer.size(); i += 4) {
            decimated2BufferDecimated.append(decimated2Buffer.at(i));
        }
        for (int i = 3; i < fourthArrayBuffer.size(); i += 4) {
            fourthArrayBufferDecimated.append(fourthArrayBuffer.at(i));
        }
        for (int i = 3; i < sixthArrayBuffer.size(); i += 4) {
            sixthArrayBufferDecimated.append(sixthArrayBuffer.at(i));
        }

        //to update 'Actual Frequency'
        if (!freqBufferDecimated.isEmpty()) {
            actualfrequency = static_cast<double>(freqBufferDecimated.first());
        } else {
            //very unlikely
            //outputStr.append("Actual Frequency: 0\n");
        }

        //update 'Actual Frequency' GUI display
        emit actualFrequencyUpdated(actualfrequency);

        //work out states for 16 coils combinations, produces only 120 unique states
        //MUST IMPLEMENT ONE FOR 8 COILS IF WISHED
        int cnt = qMin(decimated1BufferDecimated.size(), decimated2BufferDecimated.size());
        QList<qint32> headerArray;
        for (int i = 0; i < cnt; ++i) {
            qint32 S = decimated1BufferDecimated.at(i);
            qint32 E = decimated2BufferDecimated.at(i);
            qint32 Y = 0;
            if (S == 0)
                S = 16;
            if (E == 0)
                E = 16;
            if (S == E) {
                Y = 0;
            } else if (S < E) {
                Y = (E - 1) + 16 * (S - 1) - (((S * (S + 1)) / 2) - 1);
            } else {
                Y = 16 * (E - 1) - ((((E - 1) * E) / 2) - 1) + (S - E - 1);
            }
            headerArray.append(Y);
        }

        //checs common minimum size of all arrays
        int minSize = qMin(headerArray.size(),
                           qMin(decimated2BufferDecimated.size(),
                                qMin(decimated1BufferDecimated.size(),
                                     qMin(fourthArrayBufferDecimated.size(),
                                          qMin(sixthArrayBufferDecimated.size(), freqBufferDecimated.size())))));

        //store data to global2DArray for passing to main thread
        QVector<QVector<double>> global2DArray(6);
        for (int i = 0; i < minSize; ++i) {
            global2DArray[0].append(static_cast<double>(headerArray.at(i)));                    //State
            global2DArray[1].append(static_cast<double>(decimated2BufferDecimated.at(i)));      //Sensing coil
            global2DArray[2].append(static_cast<double>(decimated1BufferDecimated.at(i)));      //Excitation coil
            global2DArray[3].append(fourthArrayBufferDecimated.at(i));                          //Real
            global2DArray[4].append(sixthArrayBufferDecimated.at(i));                           //Imaginary
            global2DArray[5].append(static_cast<double>(freqBufferDecimated.at(i)));            //Actual Frequency
        }

        //pass formatted table to main thread
        emit processedChunkResult(global2DArray);
    }
}
