#include "../include/ui/ui.hpp"

#include <QApplication>
#include <QMainWindow>

#include <QWidget>
#include <QLabel>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QMainWindow window;

    window.setWindowTitle("Nyva");
    window.resize(1280, 720);

    QLabel* label = nullptr;
    createUI(window, label);
    
    window.show();

    return app.exec();
}