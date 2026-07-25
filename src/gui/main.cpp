#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    std::setlocale(LC_NUMERIC, "C");
    
    MainWindow window;
    if (!window.isStartingMinimized()) {
        window.show();
    }

    return app.exec();
}
