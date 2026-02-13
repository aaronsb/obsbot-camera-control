#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow window;
    if (!window.isStartingMinimized()) {
        window.show();
    }

    return app.exec();
}
