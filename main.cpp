#include "mainwindow.h"
#include <QApplication>
#include <QMetaType>
#include <QVector>

/**
 * @brief qMain
 * Entry point of application
 * Initialises QApplication and creates an instance of MainWindow
 * Displays main window, and enters event loop
 */

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    //register 2D vector type so it can be used in queued connections for sharing resources between threads
    qRegisterMetaType<QVector<QVector<double>>>("QVector<QVector<double>>");
    MainWindow w;
    w.show();
    return a.exec();
}
