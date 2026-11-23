#include <QApplication>
#include "MainWindow.h"
#include <clocale>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Config.cpp parses decimals with std::stod, which follows LC_NUMERIC.
    // QApplication sets the locale from the environment, so a comma-decimal
    // locale would misread "0.65" as 0. Keep numbers in the C locale.
    std::setlocale(LC_NUMERIC, "C");

    MainWindow window;
    if (!window.isStartingMinimized()) {
        window.show();
    }

    return app.exec();
}
