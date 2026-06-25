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
#include <QSlider>

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
    QWidget *startPage = new QWidget();
    QWidget *editorPage = new QWidget();
    QVBoxLayout *startLayout = new QVBoxLayout(startPage);
    QVBoxLayout *editorLayout = new QVBoxLayout(editorPage);

    // Start page
    startLayout->setSpacing(30);
    startLayout->addStretch();

    label = new QLabel("Nyva - Select File");
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
    stateButtons->setAlignment(Qt::AlignTop);
    stateButtons->setContentsMargins(10, 5, 10, 5);

    QPushButton* playToggle = new QPushButton("▶");
    playToggle->setMinimumHeight(40);
    playToggle->setStyleSheet("color: white; font-size: 15px;");

    auto togglePlay = [playToggle]() {
        bool wasPlaying = playerState.isPlaying.load();

        if (wasPlaying) {
            double mediaNow = playerState.mediaTime();
            playerState.mediaClock.store(mediaNow);
            playerState.clockRunning = false;
        }

        playerState.isPlaying = !wasPlaying;

        if (playerState.isPlaying) {
            playerState.clockRunning = false;
        }

        playToggle->setText(playerState.isPlaying ? "⏸" : "▶");
    };

    QObject::connect(playToggle, &QPushButton::clicked, togglePlay);

    QShortcut *spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), editorPage);
    QObject::connect(spaceShortcut, &QShortcut::activated, togglePlay);

    QSlider *volumeSlider = new QSlider(Qt::Horizontal);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(100);
    volumeSlider->setFixedWidth(200);

    QLabel *volLabel = new QLabel("100%");
    volLabel->setStyleSheet("color: white; font-size: 15px;");
    volLabel->setFixedWidth(volLabel->sizeHint().width());
    volLabel->setContentsMargins(0,0,0,0);

    QObject::connect(volumeSlider, &QSlider::valueChanged, [volLabel](int v) {
        playerState.volume = v * 0.01;
        volLabel->setText(QString::number(v) + "%");
    });

    QLabel* speedLabel = new QLabel("1.00x");
    speedLabel->setStyleSheet("color: white; font-size: 15px;");
    speedLabel->setFixedWidth(speedLabel->sizeHint().width());
    speedLabel->setContentsMargins(0,0,0,0);

    QSlider *speedSlider = new QSlider(Qt::Horizontal);
    speedSlider->setRange(25, 400); // 0.25x - 4.0x
    speedSlider->blockSignals(true);
    speedSlider->setValue(100);
    speedSlider->blockSignals(false);

    QObject::connect(speedSlider, &QSlider::valueChanged, [speedLabel](int v){
        double newSpeed = v / 100.0;

        if (playerState.clockRunning.load()) {
            double now = playerState.clockRunning.load() ? playerState.wallClock.elapsed() / 1000.0 : 0.0;
            double last = playerState.lastWallSec.load();

            double oldSpeed = playerState.playbackSpeed.load();
            double delta = now - last;

            playerState.mediaClock.store(
                playerState.mediaClock.load() + delta * oldSpeed
            );

            playerState.lastWallSec = now;
        }

        playerState.playbackSpeed = newSpeed;

        speedLabel->setText(QString::number(v / 100.0) + "x");
    });

    QWidget *rightWidget = new QWidget();
    QHBoxLayout *rightLayout = new QHBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0,0,0,0);
    rightLayout->setSpacing(2);

    rightLayout->addWidget(volLabel);
    rightLayout->addWidget(volumeSlider);

    rightLayout->addWidget(speedLabel);
    rightLayout->addWidget(speedSlider);
    
    rightWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    QWidget *leftMirror = new QWidget();
    leftMirror->setFixedWidth(rightWidget->sizeHint().width());

    stateButtons->addWidget(leftMirror);
    stateButtons->addStretch(1);
    stateButtons->addWidget(playToggle);
    stateButtons->addStretch(1);
    stateButtons->addWidget(rightWidget);

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
            playerState.lastWallSec = 0.0;
            playerState.clockRunning = true;
        }

        // Only pop when it's time to show this frame
        double mediaNow = playerState.mediaTime();

        FrameQueue::Entry entry;
        bool haveFrame = false;

        while (playerState.frames.peekPts(pts)) {
            double target = pts - playerState.startPts.load();

            if (target < mediaNow - 0.05) {
                playerState.frames.pop(entry); // drop old frame
                continue;
            }

            if (target > mediaNow) {
                return;
            }

            haveFrame = playerState.frames.pop(entry);

            break;
        }

        if (!haveFrame) return;

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
        playerState.stopRequested = true;

        auto stopThread = [](QThread *th) {
            if (th) { th->wait(); delete th; }
        };

        stopThread(demuxTh); demuxTh = nullptr;
        stopThread(videoTh); videoTh = nullptr;
        stopThread(audioTh); audioTh = nullptr;
        
        playerState.reset();

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
        demuxTh->fmt = fmt;
        demuxTh->videoStream = videoStream;
        demuxTh->audioStream = audioStream;
        demuxTh->state = &playerState;

        videoTh = new VideoDecodeThread();
        videoTh->codecCtx = videoCtx;
        videoTh->timeBase = fmt->streams[videoStream]->time_base;
        videoTh->state = &playerState;

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

    QObject::connect(qApp, &QApplication::aboutToQuit, [=]() {
        if (renderTimer) renderTimer->stop();

        playerState.stopRequested = true;
        playerState.isPlaying = true;

        playerState.videoPackets.flush();
        playerState.audioPackets.flush();
        playerState.frames.flush();

        auto stopThread = [](QThread *th) {
            if (th) { th->wait(); delete th; }
        };

        stopThread(demuxTh); demuxTh = nullptr;
        stopThread(videoTh); videoTh = nullptr;
        stopThread(audioTh); audioTh = nullptr;
    });

    return stack;
}