#include "../include/ui/ui.hpp"

#include <QApplication>
#include <QMainWindow>

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QString>
#include <QPushButton>

void openFileDialog(QMainWindow& window, QLabel* label) {
    QString file = QFileDialog::getOpenFileName(
        &window,
        "Open File",
        "",
        "Video Files (*.mp4 *.mov *.mkv)"
    );

    if (!file.isEmpty()) {
        label->setText("Selected File: " + file);
    }
}

QWidget* createUI(QMainWindow& window, QLabel*& label) {
    QWidget* central = new QWidget();
    window.setCentralWidget(central);

    central->setStyleSheet("background-color: #1e1e1e;");

    QVBoxLayout* layout = new QVBoxLayout(central);
    layout->setSpacing(30);

    layout->addStretch();

    label = new QLabel("Selected File: none");
    label->setStyleSheet("color: white; font-size: 30px; font-weight: bold;");
    layout->addWidget(label, 0, Qt::AlignCenter);

    QPushButton* btn = new QPushButton("Open File");
    btn->setStyleSheet("color: white; font-size: 25px;");
    layout->addWidget(btn, 0, Qt::AlignCenter);

    layout->addStretch();

    QObject::connect(btn, &QPushButton::clicked, [&window, label]() {
        openFileDialog(window, label);
    });

    return central;
}