#pragma once

#include "player.hpp"

#include <QThread>
#include <QAudioSink>
#include <QIODevice>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
    #include <libswresample/swresample.h>
}

class DemuxThread : public QThread {
    public:
        AVFormatContext *fmt = nullptr;
        int videoStream = -1;
        int audioStream = -1;
        PlayerState *state = nullptr;

    protected:
        void run() override {
            AVPacket *pkt = av_packet_alloc();

            while (!state->stopRequested) {
                if (!state->isPlaying) {
                    double speed = state->playbackSpeed.load();
                    QThread::msleep(int(5 / std::max(speed, 0.25)));
                    continue;
                }

                int ret = av_read_frame(fmt, pkt);
                if (ret < 0) break; // EOF or error

                if (pkt->stream_index == videoStream) {
                    AVPacket *p = av_packet_clone(pkt);
                    state->videoPackets.push(p);
                } else if (pkt->stream_index == audioStream) {
                    AVPacket *p = av_packet_clone(pkt);
                    state->audioPackets.push(p);
                }

                av_packet_unref(pkt);
            }

            av_packet_free(&pkt);
            state->demuxDone = true;

            // Unblock consumers so they can drain and exit
            state->videoPackets.flush();
            state->audioPackets.flush();
        }
};

class VideoDecodeThread : public QThread {
    public:
        AVCodecContext *codecCtx = nullptr;
        AVRational timeBase = {1, 1};
        PlayerState *state = nullptr;

    protected:
        void run() override {
            SwsContext *sws = nullptr;
            AVFrame *frame = av_frame_alloc();
            AVFrame *rgbFrame = av_frame_alloc();
            uint8_t *buffer = nullptr;
            int lastW = 0, lastH = 0;

            while (!state->stopRequested) {
                if (!state->isPlaying) {
                    double speed = state->playbackSpeed.load();
                    QThread::msleep(int(5 / std::max(speed, 0.25)));
                    continue;
                }

                AVPacket *pkt = state->videoPackets.pop();
                if (!pkt) {
                    // flushing
                    avcodec_send_packet(codecCtx, nullptr);
                } else {
                    if (avcodec_send_packet(codecCtx, pkt) < 0) {
                        av_packet_free(&pkt);
                        continue;
                    }

                    av_packet_free(&pkt);
                }

                while (true) {
                    int ret = avcodec_receive_frame(codecCtx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    if (ret < 0) break;

                    int W = frame->width, H = frame->height;

                    // Rebuild sws if resolution changed (or first frame)
                    if (W != lastW || H != lastH) {
                        if (sws) sws_freeContext(sws);
                        if (buffer) av_free(buffer);

                        av_frame_unref(rgbFrame);

                        sws = sws_getContext(
                            W, H, (AVPixelFormat)frame->format,
                            W, H, AV_PIX_FMT_RGB24,
                            SWS_BILINEAR, nullptr, nullptr, nullptr
                        );

                        int sz = av_image_get_buffer_size(AV_PIX_FMT_RGB24, W, H, 1);

                        buffer = (uint8_t*)av_malloc(sz);

                        av_image_fill_arrays(
                            rgbFrame->data, rgbFrame->linesize,
                            buffer, AV_PIX_FMT_RGB24, W, H, 1
                        );

                        lastW = W; lastH = H;
                    }

                    sws_scale(
                        sws,
                        frame->data, frame->linesize, 0, H,
                        rgbFrame->data, rgbFrame->linesize
                    );

                    // Deep-copy into QImage
                    QImage img(
                        rgbFrame->data[0], W, H,
                        rgbFrame->linesize[0],
                        QImage::Format_RGB888
                    );

                    QImage owned = img.copy();
                    
                    double pts = (frame->pts != AV_NOPTS_VALUE)
                                ? frame->pts * av_q2d(timeBase)
                                : 0.0;

                    state->frames.push(std::move(owned), pts);
                }

                if (!pkt && state->demuxDone) break; // flushed + EOF
            }

            if (sws) sws_freeContext(sws);
            if (buffer) av_free(buffer);

            av_frame_free(&frame);
            av_frame_free(&rgbFrame);

            state->frames.flush(); // unblock the render timer
        }
};

class AudioThread : public QThread {
public:
    AVCodecContext *audioCtx = nullptr;
    PlayerState *state = nullptr;

protected:
    void run() override {
        QAudioFormat format;

        format.setSampleRate(48000);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Int16);

        QAudioSink *sink = new QAudioSink(format);
        QIODevice *device = sink->start();

        SwrContext *swr = nullptr;
        AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
        double lastSwrSpeed = 1.0;

        auto rebuildSwr = [&](double speed) {
            if (swr) swr_free(&swr);

            int scaledInRate = (int)(audioCtx->sample_rate * speed);

            swr_alloc_set_opts2(
                &swr,
                &outLayout, AV_SAMPLE_FMT_S16, 48000,
                &audioCtx->ch_layout, audioCtx->sample_fmt, scaledInRate,
                0, nullptr
            );

            swr_init(swr);
            lastSwrSpeed = speed;
        };

        rebuildSwr(1.0);

        AVFrame *af = av_frame_alloc();

        while (!state->stopRequested) {
            if (!state->isPlaying) {
                double speed = state->playbackSpeed.load();
                QThread::msleep(int(5 / std::max(speed, 0.25)));
                continue;
            }

            if (state->audioReset) {
                sink->stop();
                device->close();

                delete sink;

                sink = new QAudioSink(format);
                device = sink->start();

                state->audioReset = false;
            }

            AVPacket *pkt = state->audioPackets.pop();

            if (!pkt) {
                avcodec_send_packet(audioCtx, nullptr); // flush
            } else {
                avcodec_send_packet(audioCtx, pkt);
                av_packet_free(&pkt);
            }

            while (avcodec_receive_frame(audioCtx, af) == 0) {
                double currentSpeed = state->playbackSpeed.load();
                if (currentSpeed != lastSwrSpeed) {
                    rebuildSwr(currentSpeed);
                }

                // Sync
                if (state->clockRunning && !state->stopRequested) {
                    double audioPts = (af->pts != AV_NOPTS_VALUE)
                        ? af->pts * av_q2d(audioCtx->pkt_timebase)
                        : 0.0;
                    
                    double target = audioPts - state->startPts.load();

                    while (state->clockRunning && !state->stopRequested) {
                        double mediaNow = state->mediaTime();
                        double diffMs = (target - mediaNow) * 1000.0;

                        if (diffMs <= 5.0) break; // audio is at or behind target, go ahead and write

                        msleep(std::min(diffMs, 10.0));
                    }
                }

                int outSamples = swr_get_out_samples(swr, af->nb_samples);
                uint8_t *outData = nullptr;
                av_samples_alloc(&outData, nullptr, 2, outSamples, AV_SAMPLE_FMT_S16, 0);

                int written = swr_convert(
                    swr, &outData, outSamples,
                    (const uint8_t **)af->data, af->nb_samples
                );

                if (written > 0) {
                    int16_t *samples = (int16_t*)outData;
                    
                    int totalSamples = written * 2; // stereo = 2 channels

                    float volume = state->volume.load();

                    for (int i = 0; i < totalSamples; i++) {
                        int val = (int)(samples[i] * volume);

                        if (val > 32767) val = 32767;
                        if (val < -32768) val = -32768;

                        samples[i] = (int16_t)val;
                    }
                        
                    device->write((const char *)outData, totalSamples * sizeof(int16_t));
                }

                av_freep(&outData);
            }

            if (!pkt && state->demuxDone) break;
        }

        av_frame_free(&af);
        swr_free(&swr);
        sink->stop();
        delete sink;
    }
};