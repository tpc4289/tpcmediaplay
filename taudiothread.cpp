#include "taudiothread.h"

#include "tresample.h"
#include "tdecode.h"
#include "taudioiodevice.h"
#include "tpacketqueue.h"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QAudioSink>
#include <QDebug>

extern "C"{
#include <libavcodec/avcodec.h>
}

TAudioThread::TAudioThread(QObject *parent)
    : QThread{parent}
{}

TAudioThread::~TAudioThread()
{

}

//获取解码类对象和音频包队列对象
void TAudioThread::init(TDecode *decode, TPacketQueue *queue)
{
    p_decode = decode;
    p_packetQueue = queue;
}

bool TAudioThread::takeOne(AVFrame *frame)
{
    AVFrame *pcm = p_sample->resample(frame);
    if(!pcm)
        return true;

    //背压：缓冲满时等待声卡消费；同时响应停止/seek
    while(!p_audioDev->pushQueue(pcm)){
        av_frame_free(&pcm);

        msleep(10);

        pcm = p_sample->resample(frame);
        if(!pcm)
            return true;
        return false;
    }
    av_frame_free(&pcm);
    return true;

}

//开始运行
void TAudioThread::run()
{
    qDebug()<<"TAudioThread start";
    //设备与sink
    QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    //选择输出设备
    QAudioFormat fmt = chooseFormat(dev);
    if(fmt.sampleFormat() != QAudioFormat::Int16)
        qDebug()<<"warning: device has no s16 format, using preferred";

    //重新采样:输出与sink一致
    p_sample = new TResample();
    AVCodecContext *ac = p_decode ? p_decode->codecContext() : nullptr;
    if(!ac || !p_sample->open(ac, fmt.sampleRate(), fmt.channelCount())){
        qDebug() << "TAudioOutput: resample open failed, audio disabled";
        return;
    }

    p_audioDev = new TAudioIODevice();

    p_sink = new QAudioSink(dev, fmt);
    p_sink->start(p_audioDev);
    connect(p_sink, &QAudioSink::stateChanged, this, [](QtAudio::State s) {
        qDebug() << "sink state changed:" << s;
    });
    if(p_sink->error() != QtAudio::NoError){
        qDebug()<<"TAudioOutput: sink start failed";
        return;
    }

    for(;;){
        TPacket *tp = nullptr;
        if(!p_packetQueue->pop(tp)){
            if (isInterruptionRequested()) break;
            msleep(5);
            continue;
        }
        if(!tp)
            continue;

        //EOF 标志包：flush 解码器取净尾帧后，等缓冲播完再退出
        if(!tp->pkt){
            delete tp;
            p_decode->flushDecoder();
            while (AVFrame *tf = p_decode->recv()) {
                takeOne(tf);
                av_frame_free(&tf);
            }
            // for(;;){
            //     AVFrame *tf = p_decode->recv();
            //     if(!tf) break;
            //     bool cont = takeOne(tf);
            //     av_frame_free(&tf);
            //     if(!cont) break;
            // }
            // 等缓冲播完再退出
            // while (p_audioDev->bytesFree() < p_audioDev->bufferSize()) {
            //     if (isInterruptionRequested()) break;
            //     msleep(10);
            // }
            break;
        }

        int ret = p_decode->send(tp->pkt);
        delete tp;

        if (ret == AVERROR(EAGAIN)) {
            // 解码器需要更多数据，继续取下一个 packet
            continue;
        }
        if (ret < 0) {
            char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, buf, sizeof(buf));
            qDebug() << "avcodec_send_packet failed:" << buf;
            continue;
        }

        for(;;){
            AVFrame *af = p_decode->recv();
            if(!af){
                break;
            }
            bool cont = takeOne(af);
            av_frame_free(&af);
            if(!cont) break;

            if(ret == AVERROR(EAGAIN)){
                p_decode->resend();
                continue;
            }

            break;
        }
        int64_t c = p_audioDev->getClock();
    }

}

//选择设备支持的 s16 输出格式
QAudioFormat TAudioThread::chooseFormat(const QAudioDevice &dev)
{
    //首选 44100/48000 双声道 s16（Windows 设备几乎都支持）
    for(int rate : {48000, 44100}){
        for(int ch : {2, 1}){
            QAudioFormat fmt;
            fmt.setSampleFormat(QAudioFormat::Int16);
            fmt.setSampleRate(rate);
            fmt.setChannelCount(ch);
            if(dev.isFormatSupported(fmt)){
                qDebug()<<"audio format chosen: rate = "<<rate<<", channel = "<<ch;
                return fmt;
            }
        }
    }
    //兜底：设备首选格式（若非 s16 也照用，重采样仍转 s16，多数后端可隐式处理）
    qDebug() << "fallback to device preferred format";
    return dev.preferredFormat();
}
