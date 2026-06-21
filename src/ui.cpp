#include "../include/ui/ui.hpp"

#include <QApplication>
#include <QMainWindow>

#include <QStackedWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QString>
#include <QPushButton>

QString openFileDialog(QMainWindow& window) {
    QString file = QFileDialog::getOpenFileName(
        &window,
        "Open File",
        "",
        "Video Files (*.mp4 *.mov *.mkv)"
    );

    return file;
}

QWidget* createUI(QMainWindow& window, QLabel*& label) {
    QStackedWidget* stack = new QStackedWidget();
    window.setCentralWidget(stack);

    stack->setStyleSheet("background-color: #1e1e1e;");

    QWidget *startPage = new QWidget();
    QWidget *editorPage = new QWidget();

    QVBoxLayout* startLayout = new QVBoxLayout(startPage);
    QVBoxLayout* editorLayout = new QVBoxLayout(editorPage);

    startLayout->setSpacing(30);

    startLayout->addStretch();

    label = new QLabel("Selected File: none");
    label->setStyleSheet("color: white; font-size: 30px; font-weight: bold;");
    startLayout->addWidget(label, 0, Qt::AlignCenter);

    QPushButton* btn = new QPushButton("Open File");
    btn->setStyleSheet("color: white; font-size: 25px;");
    startLayout->addWidget(btn, 0, Qt::AlignCenter);

    startLayout->addStretch();

    QObject::connect(btn, &QPushButton::clicked, [&window, stack]() {
        QString file = openFileDialog(window);

        if (!file.isEmpty()) {
            stack->setCurrentIndex(1);
        }
    });

    QLabel *videoBox = new QLabel("Video Preview");
    videoBox->setMinimumSize(800, 450);
    videoBox->setStyleSheet("background-color: black; color: white;");
    videoBox->setAlignment(Qt::AlignCenter);

    QWidget *container = new QWidget();
    container->setFixedSize(900, 500);

    QVBoxLayout *containerLayout = new QVBoxLayout(container);
    containerLayout->addWidget(videoBox);

    QVBoxLayout *wrapper = new QVBoxLayout();
    wrapper->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    wrapper->addWidget(container);

    editorLayout->addLayout(wrapper);

    stack->addWidget(startPage);
    stack->addWidget(editorPage);

    return stack;
}