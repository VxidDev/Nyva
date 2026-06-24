#pragma once

#include <QImage>
#include <QMutex>
#include <QWaitCondition>
#include <QElapsedTimer>
#include <queue>
#include <atomic>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
    #include <libswresample/swresample.h>
}

struct PacketQueue {
    static constexpr int MAX = 64;

    std::queue<AVPacket*> q;
    QMutex mu;
    QWaitCondition notFull;
    QWaitCondition notEmpty;
    bool flushing = false;

    // Push a packet; blocks if full (unless flushing)
    void push(AVPacket *pkt) {
        QMutexLocker lk(&mu);

        while ((int)q.size() >= MAX && !flushing)
            notFull.wait(&mu);

        if (flushing) { 
            av_packet_free(&pkt); 
            return;
        }

        q.push(pkt);
        notEmpty.notify_one();
    }

    // Pop a packet; blocks until one is available or flushing
    // Returns nullptr when flushing and queue is empty
    AVPacket* pop() {
        QMutexLocker lk(&mu);

        while (q.empty() && !flushing)
            notEmpty.wait(&mu);

        if (q.empty()) 
            return nullptr;

        AVPacket *p = q.front();
        q.pop();

        notFull.notify_one();
        return p;
    }

    void flush() {
        QMutexLocker lk(&mu);

        flushing = true;

        while (!q.empty()) {
            av_packet_free(&q.front());
            q.pop();
        }

        notFull.notify_all();
        notEmpty.notify_all();
    }
};

struct FrameQueue {
    static constexpr int MAX = 8;

    struct Entry { QImage img; double pts; };
    std::queue<Entry> q;
    QMutex mu;
    QWaitCondition notFull;
    QWaitCondition notEmpty;
    std::atomic<bool> flushing{false};

    void push(QImage img, double pts) {
        QMutexLocker lk(&mu);

        while ((int)q.size() >= MAX && !flushing)
            notFull.wait(&mu);

        if (flushing) 
            return;

        q.push({std::move(img), pts});

        notEmpty.notify_one();
    }

    bool pop(Entry &out) {
        QMutexLocker lk(&mu);

        while (q.empty() && !flushing)
            notEmpty.wait(&mu);

        if (q.empty()) 
            return false;

        out = std::move(q.front()); q.pop();
        notFull.notify_one();

        return true;
    }

    // Non-blocking peek at front PTS
    bool peekPts(double &pts) {
        QMutexLocker lk(&mu);

        if (q.empty()) 
            return false;

        pts = q.front().pts;
        return true;
    }

    void flush() {
        flushing = true;

        notFull.notify_all();
        notEmpty.notify_all();

        QMutexLocker lk(&mu);

        while (!q.empty()) q.pop();

        flushing = false;
    }
};

struct PlayerState {
    // Queues
    PacketQueue videoPackets;
    PacketQueue audioPackets;
    FrameQueue  frames;

    // Sync clock: set to elapsed ms when first video PTS is anchored
    QElapsedTimer wallClock;
    std::atomic<double> startPts{-1.0};  // PTS seconds of first frame
    std::atomic<bool> clockRunning{false};

    // EOF / stop signals
    std::atomic<bool> demuxDone{false};
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> isPlaying{false};
    std::atomic<bool> audioReset{false};

    // Sound 
    std::atomic<float> volume = 1.0f;
    
    void reset() {
        stopRequested = true;

        videoPackets.flush();
        audioPackets.flush();
        frames.flush();

        stopRequested = false;

        demuxDone = false;
        clockRunning = false;
        startPts = -1.0;
        isPlaying = false;
        audioReset = false;

        videoPackets.flushing = false;
        audioPackets.flushing = false;
        frames.flushing = false;
    }

    double elapsedSecs() const {
        if (!clockRunning) return 0.0;
        return wallClock.elapsed() / 1000.0;
    }
};