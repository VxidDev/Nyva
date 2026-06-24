#include "../include/ui/ui.hpp"
#include "../include/player.hpp"
#include "../include/threads.hpp"

#include <QApplication>
#include <QMainWindow>
#include <QStackedWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QString>
#include <QPushButton>
#include <QTimer>
#include <QShortcut>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
}

static constexpr int DISPLAY_W = 960;
static constexpr int DISPLAY_H = 540;

static QString openFileDialog(QMainWindow &window) {
    return QFileDialog::getOpenFileName(
        &window, "Open File", "",
        "Video Files (*.mp4 *.mov *.mkv)"
    );
}

QWidget* createUI(QMainWindow &window, QLabel *&label) {
    static PlayerState playerState;
    static AVFormatContext *fmt = nullptr;
    static AVCodecContext *videoCtx = nullptr;
    static AVCodecContext *audioCtx = nullptr;
    static DemuxThread *demuxTh = nullptr;
    static VideoDecodeThread *videoTh = nullptr;
    static AudioThread *audioTh = nullptr;
    static QTimer *renderTimer = nullptr;
    static QLabel *videoBox = nullptr;

    // Root
    QStackedWidget *stack = new QStackedWidget();
    window.setCentralWidget(stack);
    stack->setStyleSheet("background-color: #1e1e1e;");

    // Pages
    QWidget *startPage  = new QWidget();
    QWidget *editorPage = new QWidget();
    QVBoxLayout *startLayout  = new QVBoxLayout(startPage);
    QVBoxLayout *editorLayout = new QVBoxLayout(editorPage);

    // Start page
    startLayout->setSpacing(30);
    startLayout->addStretch();

    label = new QLabel("Selected File: none");
    label->setStyleSheet("color: white; font-size: 30px; font-weight: bold;");
    startLayout->addWidget(label, 0, Qt::AlignCenter);

    QPushButton *btn = new QPushButton("Open File");
    btn->setStyleSheet("color: white; font-size: 25px;");
    startLayout->addWidget(btn, 0, Qt::AlignCenter);

    startLayout->addStretch();

    // Editor page
    videoBox = new QLabel();
    videoBox->setFixedSize(DISPLAY_W, DISPLAY_H);
    videoBox->setStyleSheet("background-color: black;");
    videoBox->setAlignment(Qt::AlignCenter);

    QVBoxLayout *wrapper = new QVBoxLayout();
    wrapper->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    wrapper->addWidget(videoBox);
    editorLayout->addLayout(wrapper);

    QHBoxLayout *stateButtons = new QHBoxLayout();
    stateButtons->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    QPushButton* playToggle = new QPushButton("▶");
    playToggle->setStyleSheet("color: white; font-size: 15px;");

    auto togglePlay = [playToggle]() {
        playerState.isPlaying = !playerState.isPlaying;

        if (playerState.isPlaying) {
            playerState.clockRunning = false;
        }

        playToggle->setText(playerState.isPlaying ? "⏸" : "▶");
    };

    QObject::connect(playToggle, &QPushButton::clicked, togglePlay);

    QShortcut *spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), editorPage);
    QObject::connect(spaceShortcut, &QShortcut::activated, togglePlay);

    stateButtons->addWidget(playToggle);
    editorLayout->addLayout(stateButtons);

    stack->addWidget(startPage);
    stack->addWidget(editorPage);

    // Render timer (UI thread only, just shows pre-decoded frames)
    renderTimer = new QTimer();
    renderTimer->setInterval(1); // poll at ~1ms; PTS logic controls actual display rate

    QObject::connect(renderTimer, &QTimer::timeout, [=]() {
        if (!playerState.isPlaying) return;

        double pts = 0.0;
        if (!playerState.frames.peekPts(pts)) return;

        // Anchor the clock on the very first frame
        if (!playerState.clockRunning.load()) {
            playerState.startPts = pts;
            playerState.wallClock.restart();
            playerState.clockRunning = true;
        }

        // Only pop when it's time to show this frame
        double targetSecs  = pts - playerState.startPts.load();
        double elapsedSecs = playerState.elapsedSecs();

        if (elapsedSecs < targetSecs - 0.002) return; // wait

        FrameQueue::Entry entry;
        if (!playerState.frames.pop(entry)) return;

        videoBox->setPixmap(
            QPixmap::fromImage(entry.img).scaled(
                videoBox->size(),
                Qt::KeepAspectRatioByExpanding,
                Qt::SmoothTransformation
            )
        );
    });

    QObject::connect(btn, &QPushButton::clicked, [&window, stack]() {
        QString file = openFileDialog(window);
        if (file.isEmpty()) return;

        // Stop previous session
        if (renderTimer->isActive()) renderTimer->stop();
        playerState.reset();

        auto stopThread = [](QThread *th) {
            if (th) { th->wait(); delete th; }
        };

        stopThread(demuxTh); demuxTh = nullptr;
        stopThread(videoTh); videoTh = nullptr;
        stopThread(audioTh); audioTh = nullptr;

        if (fmt) avformat_close_input(&fmt);

        if (videoCtx) { 
            avcodec_free_context(&videoCtx);
            videoCtx = nullptr;
        }

        if (audioCtx) { 
            avcodec_free_context(&audioCtx); 
            audioCtx = nullptr; 
        }

        // Open container
        if (avformat_open_input(&fmt, file.toStdString().c_str(), nullptr, nullptr) < 0) return;
        avformat_find_stream_info(fmt, nullptr);

        int videoStream = -1, audioStream = -1;

        for (unsigned i = 0; i < fmt->nb_streams; i++) {
            auto t = fmt->streams[i]->codecpar->codec_type;

            if (t == AVMEDIA_TYPE_VIDEO && videoStream < 0)
                videoStream = (int)i;
            if (t == AVMEDIA_TYPE_AUDIO && audioStream < 0) 
                audioStream = (int)i;
        }

        if (videoStream < 0) 
            return;

        AVCodecParameters *par = fmt->streams[videoStream]->codecpar;
        const AVCodec *codec   = avcodec_find_decoder(par->codec_id);
        videoCtx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(videoCtx, par);
        videoCtx->thread_count = QThread::idealThreadCount();
        videoCtx->thread_type  = FF_THREAD_FRAME;
        avcodec_open2(videoCtx, codec, nullptr);

        // Audio codec
        if (audioStream >= 0) {
            AVCodecParameters *par = fmt->streams[audioStream]->codecpar;
            const AVCodec *codec   = avcodec_find_decoder(par->codec_id);
            audioCtx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(audioCtx, par);
            audioCtx->pkt_timebase = fmt->streams[audioStream]->time_base;
            avcodec_open2(audioCtx, codec, nullptr);
        }

        // Start threads
        demuxTh = new DemuxThread();
        demuxTh->fmt         = fmt;
        demuxTh->videoStream = videoStream;
        demuxTh->audioStream = audioStream;
        demuxTh->state       = &playerState;

        videoTh = new VideoDecodeThread();
        videoTh->codecCtx  = videoCtx;
        videoTh->timeBase  = fmt->streams[videoStream]->time_base;
        videoTh->state     = &playerState;

        demuxTh->start();
        videoTh->start();

        if (audioCtx) {
            audioTh = new AudioThread();
            audioTh->audioCtx = audioCtx;
            audioTh->state    = &playerState;
            audioTh->start();
        }

        renderTimer->start();
        stack->setCurrentIndex(1);
    });

    return stack;
}