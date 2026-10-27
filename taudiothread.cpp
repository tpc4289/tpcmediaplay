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
    // 先请求终止，等待线程退出
    requestInterruption();
    wait();
}

//获取解码类对象和音频包队列对象
void TAudioThread::init(TDecode *decode, TPacketQueue *queue)
{
    p_decode = decode;
    p_packetQueue = queue;
}

bool TAudioThread::takeOne(AVFrame *frame)
{
    if (!frame || !p_sample || !p_audioDev)
        return false;
    AVFrame *pcm = p_sample->resample(frame);
    if(!pcm){
        qDebug() << "TAudioThread::takeOne resample return null";
        return true;
    }
    //背压：缓冲满时等待声卡消费；同时响应停止/seek
    while(!p_audioDev->pushQueue(pcm)){
        av_frame_free(&pcm);

        msleep(10);

        // pcm = p_sample->resample(frame);
        // if(!pcm)
        //     return true;
        return false;
    }
    // while(!isInterruptionRequested()){
    //     if(p_audioDev->pushQueue(pcm)){
    //         qDebug() << "push ok, pcm samples:" << pcm->nb_samples << " rate:" << pcm->sample_rate << " ch:" << pcm->ch_layout.nb_channels;
    //         break;
    //     }
    //     //缓存满，短暂失眠，重试
    //     msleep(10);
    // }
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
        delete p_sample;
        p_sample = nullptr;
        return;
    }

    p_audioDev = new TAudioIODevice();

    p_sink = new QAudioSink(dev, fmt);

    connect(p_sink, &QAudioSink::stateChanged, this, [](QtAudio::State s) {
        qDebug() << "sink state changed:" << s;
    },Qt::QueuedConnection);
    p_sink->start(p_audioDev);
    if(p_sink->error() != QtAudio::NoError){
        qDebug()<<"TAudioOutput: sink start failed";
        return;
    }

    while(!isInterruptionRequested()){
        TPacket *tp = nullptr;
        if(!p_packetQueue->pop(tp)){
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
            // 等待缓冲区播放完毕
            while (!isInterruptionRequested())
            {
                if (p_audioDev->getBytesAvailable() <= 0)
                    break;
                msleep(10);
            }
            break;
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

        if(ret < 0){
            if(ret == AVERROR(EAGAIN)){
                // 解码器满了，先收帧，本轮包保留，下一轮重发
                qDebug() << "avcodec_send_packet EAGAIN";
            }else{
                char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, buf, sizeof(buf));
                qDebug() << "avcodec_send_packet failed:" << buf;
            }
            continue;
        }


        while(AVFrame *af = p_decode->recv()){
            takeOne(af);
            av_frame_free(&af);
        }
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
