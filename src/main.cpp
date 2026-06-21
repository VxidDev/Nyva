#include <QApplication>
#include <QMainWindow>

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QString>
#include <QPushButton>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QMainWindow window;

    window.setWindowTitle("Nyva");
    window.resize(1280, 720);

    QWidget *central = new QWidget();
    window.setCentralWidget(central);

    central->setStyleSheet("background-color: #1e1e1e;");

    QVBoxLayout *layout = new QVBoxLayout(central);
    layout->setSpacing(30);

    QLabel *label = new QLabel("Selected File: none");

    label->setStyleSheet(
        "color: white;"
        "font-size: 30px;"
        "font-weight: bold;"
    );

    layout->addStretch();
    layout->addWidget(label, 0, Qt::AlignCenter);

    QPushButton *btn = new QPushButton("Open File");
    btn->setStyleSheet(
        "color: white;"
        "font-size: 25px;"
    );

    layout->addWidget(btn, 0, Qt::AlignCenter);

    QObject::connect(btn, &QPushButton::clicked, [&]() {
        QString file = QFileDialog::getOpenFileName(&window, "Open File", "", "Video Files (*.mp4 *.mov *.mkv)");
        label->setText("Selected File: " + file);
    });

    layout->addStretch();
    
    window.show();

    return app.exec();
}