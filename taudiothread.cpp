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

    // sink/设备在 GUI 线程（init）创建，线程退出后回到这里释放是安全的
    if(p_sink){
        p_sink->stop();
        delete p_sink;
        p_sink = nullptr;
    }
    if(p_sample){
        p_sample->close();
        delete p_sample;
        p_sample = nullptr;
    }
    delete p_audioDev;
    p_audioDev = nullptr;
}

//组装：解码器 + 队列 + 创建并启动音频设备
//（在 GUI 线程调用：那里有事件循环，QAudioSink 的状态通知与取数据调度才可靠；
// 原实现放在 run() 里，覆写的 run 没有 exec()，sink 容易停在 Idle 不再拉数据 → 无声）
void TAudioThread::init(TDecode *decode, TPacketQueue *queue, TMediaPlayCtrl *playCtrl)
{
    p_decode = decode;
    p_packetQueue = queue;
    p_playCtrl = playCtrl;
    m_disabled = (decode == nullptr || queue == nullptr);

    if(!p_audioDev)
        p_audioDev = new TAudioIODevice();//先建好：视频线程起播前就要拿到时钟指针
    if(m_disabled){
        qDebug()<<"TAudioThread: no audio stream/decoder, audio disabled";
        return;
    }

    QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    QAudioFormat fmt = chooseFormat(dev);
    if(fmt.sampleFormat() != QAudioFormat::Int16){
        //重采样固定输出 s16；sink 若不是 s16，数据会被按错误格式解释 → 无声或噪音
        qWarning()<<"TAudioThread: device has no s16 format, audio disabled";
        m_disabled = true;
        return;
    }

    //重新采样:输出与sink一致
    p_sample = new TResample();
    AVCodecContext *ac = p_decode->codecContext();
    if(!ac || !p_sample->open(ac, fmt.sampleRate(), fmt.channelCount())){
        qWarning() << "TAudioThread: resample open failed, audio disabled";
        delete p_sample;
        p_sample = nullptr;
        m_disabled = true;
        return;
    }

    p_sink = new QAudioSink(dev, fmt);
    connect(p_sink, &QAudioSink::stateChanged, this, [](QtAudio::State s) {
        qDebug() << "sink state changed:" << int(s);
    }, Qt::QueuedConnection);
    p_sink->start(p_audioDev);
    if(p_sink->error() != QtAudio::NoError){
        qWarning()<<"TAudioThread: sink start failed, err="<<int(p_sink->error());
        m_disabled = true;
        return;
    }

    qDebug()<<"TAudioThread: sink started, rate="<<fmt.sampleRate()<<" ch="<<fmt.channelCount();
}

//收净解码器当前所有帧,收到解码器帧就推入到队列，并推送data给sink
void TAudioThread::drainFrames()
{
    while(AVFrame *af = p_decode->recv()){
        takeOne(af); //推送到队列
        av_frame_free(&af); //释放解码帧对象
    }
}

//EOF 收尾：排空解码器 + 重采样缓存，再等缓冲播完
void TAudioThread::drainTail()
{
    //flushDecoder 可能因解码器仍满返回 false，反复取帧腾空间
    for(int i = 0; i < 8; ++i){
        if(p_decode->flushDecoder())
            break;
        drainFrames();
    }

    //接收剩余帧
    while (AVFrame *tf = p_decode->recv()) {
        takeOne(tf);
        av_frame_free(&tf);
    }

    //取重采样器内部残留的尾音，否则结尾会被截掉。
    //注意：flush() 出来的已经是目标格式的 PCM，不能再走 takeOne（那会二次重采样，
    //表现为结尾音调异常/杂音）；直接入队，并补一个可推进的时钟基准
    while (AVFrame *tf = p_sample->flush()) {
        if(tf->pts == AV_NOPTS_VALUE || tf->pts < 0)
            tf->pts = p_audioDev->getClock();
        if(tf->pts == CLOCK_NOT_READY)
            tf->pts = 0;
        bool pushed = p_audioDev->pushQueue(tf);
        av_frame_free(&tf);
        if(!pushed)
            break;      //缓冲满且已被中断，放弃尾音
    }

    //等缓冲播完再退出（用真实待播状态，不要用谎报值比较）
    while (!isInterruptionRequested()){
        if(p_audioDev->isEmpty())
            break;
        msleep(10);
    }


}
//音频不可用时也要排空包队列：否则 3MB 队列写满后解封装线程卡在 push，视频跟着停
void TAudioThread::drainQueueOnly()
{
    while(!isInterruptionRequested()){
        TPacket *tp = nullptr;
        if(!p_packetQueue->pop(tp))
            break;                 //队列已停止
        if(!tp)
            continue;
        if(!tp->pkt){              //EOF 标志包
            delete tp;
            break;
        }
        av_packet_free(&tp->pkt);
        delete tp;
    }
}

bool TAudioThread::takeOne(AVFrame *frame)
{
    if (!frame || !p_sample || !p_audioDev)
        return false;
    AVFrame *pcm = p_sample->resample(frame);
    if(!pcm){
        qDebug() << "TAudioThread::takeOne resample return null";
        return true; //重采样器内部延迟，正常情况
    }
    //真正的背压：缓冲满就等，一帧都不能丢。
    while(!isInterruptionRequested()){
        if(p_audioDev->pushQueue(pcm)){
            av_frame_free(&pcm);
            return true;
        }
        msleep(5);
    }
    //原实现 while(!pushQueue(pcm)){ free; msleep; return false; } 循环体第一次就 return，
    //队列一满即静默丢帧；且 pcm 被 free 后若仍被引用就是二次释放。
    // while(!p_audioDev->pushQueue(pcm)){
    //     av_frame_free(&pcm);

    //     msleep(10);

    //     // pcm = p_sample->resample(frame);
    //     // if(!pcm)
    //     //     return true;
    //     return false;
    // }
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

    if(!p_packetQueue){
        qWarning()<<"TAudioThread: no packet queue, exit";
        return;
    }

    //音频没有（无流/初始化失败）：只负责排空队列
    if(m_disabled){
        qDebug()<<"TAudioThread: audio disabled, draining queue only";
        drainQueueOnly();
        return;
    }

    while(!isInterruptionRequested()){

        if(p_playCtrl->stopRequest)
            break;
        //seek：清解码缓冲与播放缓冲（时钟由 requestFlushAll 复位）
        if(p_playCtrl->takeFlush(&TMediaPlayCtrl::flushAudio)){
            p_decode->clear();
            p_audioDev->clearBuffer();
            p_sink->reset();
        }

        if(p_playCtrl->paused){
            if(!pausedHandled){
                p_sink->suspend();
                p_audioDev->setPaused(true);
                pausedHandled = true;
            }
        }

        if(pausedHandled){
            p_audioDev->setPaused(false);
            p_sink->resume();
            pausedHandled = false;
        }

        TPacket *tp = nullptr;
        // if(!p_packetQueue->pop(tp)){
        //     msleep(5);
        //     continue;
        // }
        if(!p_packetQueue->pop(tp))
            break;      //pop 返回 false = 队列已停止；原来是 msleep+continue 会死循环空转
        if(!tp)
            continue;

        //EOF 标志包：flush 解码器取净尾帧后，等缓冲播完再退出
        if(!tp->pkt){
            delete tp;
            drainTail();
            // p_decode->flushDecoder();
            // while (AVFrame *tf = p_decode->recv()) {
            //     takeOne(tf);
            //     av_frame_free(&tf);
            // }
            // // 等待缓冲区播放完毕
            // while (!isInterruptionRequested())
            // {
            //     if (p_audioDev->getBytesAvailable() <= 0)
            //         break;
            //     msleep(10);
            // }
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
                //解码器满：包已克隆进 pending。先收帧腾空间，再把 pending 送回。
                //原实现直接 continue 且从不 resend，pending 永远堵在解码器入口
                drainFrames();
                while(p_decode->resend() == AVERROR(EAGAIN) && !isInterruptionRequested())
                    drainFrames();
            }else{
                char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, buf, sizeof(buf));
                qDebug() << "avcodec_send_packet failed:" << buf;
            }
            continue;
        }

        drainFrames();
        //发布音频主时钟（readData 在音频线程推进，这里拷贝共享）
        int64_t c = audioDevice()->getClock();
        if (c != CLOCK_NOT_READY)
            p_playCtrl->audioClockMs = c;
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
    //兜底：设备首选的采样率/声道，但强制 s16。
    //原实现直接返回 preferredFormat()，若是 float，与固定输出 s16 的重采样不匹配 → 无声/噪音
    QAudioFormat fmt = dev.preferredFormat();
    fmt.setSampleFormat(QAudioFormat::Int16);
    if(dev.isFormatSupported(fmt)){
        qDebug()<<"fallback to preferred rate/ch with s16:"<<fmt.sampleRate()<<fmt.channelCount();
        return fmt;
    }
    qWarning() << "no supported s16 format on this device";
    return fmt;
}
