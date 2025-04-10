#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QUdpSocket>
#include <QTimer>
#include <QVector>
#include <QStringList>
#include <QList>
#include <QQueue>
#include <QThread>

/**
 * MainWindow class
 * -----------------------------------------
 * This class handles the main window functioanlity including:
 *      UDP bi-communication
 *      Data processsing and conversion (mirroring LabVIEW's string-to-array, decimation, etc.)
 *      UI updates (logs, LCD displays, status indicators)
 * Global Buffers: Five buffers store processed data from each UDP packet
 **/

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class DataConsumer;
class ProcessingData;
class SharedBuffer;

class MainWindow : public QMainWindow
{
    Q_OBJECT        //enables Qt's signal-slot mechanisms

public:

    //constructor and Destructor
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:

    void handleIncomingMessage();                   //processes incoming UDP data on primary socket, instrument messages
    void handleDatagram();                          //processes incoming UDP data on secondary socket, instrument data
    void onLogButtonClicked();                      //writes current message log to a file

    void updateLocalPort(int newPort);              //updates local port and rebinds primary UDP socket

    //Slots for sending commands/configuration via UDP
    void onbuttonSendConfigurationclicked();        //prepares and sends configuration commands
    void onbuttonSendSensingSequenceclicked();      //prepares and sends sensing sequence
    void onbuttonSendExcitationSequenceclicked();   //prepares and sends excitation sequence
    void onbuttonSendFrequencyclicked();            //prepares and sens configuration frequency

    void oninputDeviceEnableactivated(int index);   //updates device enable status
    void onbuttonUpdateclicked();                   //updates default sequence fields based on coil selection

    void printGlobal2DArrayToTextBrowser();         //turn this off
    void onbuttonStopFinalDataclicked();            //turn this off
    void onbuttonStartFinalDataclicked();           //turn this off
    void onbuttonClearFinalDataclicked();           //turn this off
    void onbuttonSaveclicked();                     //called when SAVE button is clicked

    //retrives formatted data from dataConsumerThread for saving/discarding
    void onProcessedChunkResult(const QVector<QVector<double>> &global2DArray);

    void onbuttonSyncclicked();                     //called when SYNC button clicked

private:
    Ui::MainWindow *ui;                         //pointer to UI elements
    QUdpSocket *udpSocket;                      //UDP socket for incoming messages
    QUdpSocket *udpSocketOut;                   //UDP socket for instrument data communicaiton
    quint16 localPort;                          //gplobal variable to store local port number (from UI)
    bool messageReceivedFlag;                   //flaf to track if a message has been received (redundant)
    double storedFrequencyConfiguration;        //frequency configuration value (redundant)

    //data processing containers
    QVector<double> frequencyArray;             //frequency values from configuration
    QVector<quint16> phaseOffsetArray;          //phase offset values
    QVector<quint32> joinedValueArray;          //joined frequency values (hi << 16 | lo)

    //stage-one processed data
    QStringList formattedChunks;                //formatted chunks from UDP datagrams
    QString formattedRawData;                   //final raw string after processing
    QStringList outputTokens;                   //tokens from splitting final string
    QList<qint32>convertedIntegers;             //integers converted from tokens
    QList<qint32>finalFrequency;                //final frequency values after further processing

    //global buffers for processed data
    QQueue<qint64> bufferFinalFrequency;        //actual frequency
    QQueue<qint32> bufferDecimated1;            //sensing coils
    QQueue<qint32> bufferDecimated2;            //excitation coils
    QQueue<double> bufferFourthArrayDivided;    //real
    QQueue<double> bufferSixthArrayDivided;     //imaginary

    //global variable for autosync
    qint32 autosync = 0;

    //to receive formatted data
    QVector<QVector<double>>global2DArray;

    bool m_stopUpdates = false;                 //redundant
    bool clear2DArray = true;                   //discards formatted data if TRUE
    int setFrames = 0;                          //number of frames to save
    int framesSaved = 0;                        //number of frames saved so far

    QString csvFilePath;                        //data to save, file path

    //writes data to save file
    void appendGlobal2DArrayToCSV(const QString &filePath);

    SharedBuffer *sharedBuffer;                 //to pass data to worker threads

    ProcessingData *processingData;
    QThread *processingDataThread;

    DataConsumer *dataConsumer;
    QThread *dataConsumerThread;

    bool fileInitialised = false;               //to allow data to be saved to same file in the same saving session
    QString lastSavedFilePath = "null";         //supports the above

};
#endif // MAINWINDOW_H
