#ifndef TAUDIOTHREAD_H
#define TAUDIOTHREAD_H

#include <QThread>

class TDecode;
class TPacketQueue;
class QAudioFormat;
class QAudioDevice;
class TResample;
class QAudioSink;
class TAudioIODevice;
class TMediaPlayCtrl;

struct AVFrame;

/*
 * 音频解码线程：取包 -> 解码 -> 重采样(s16 交织) -> 写入 TAudioIODevice
 * TAudioIODevice 的消费位置即全片主时钟，视频线程据此同步（音频为主时钟）
 *
 * 注意：音频设备与 QAudioSink 在 init()（GUI 线程）里创建并启动，
 * 本线程只负责往设备里 push。原因：
 *  1) QAudioSink 依赖线程事件循环派发状态与可读通知，而 run() 覆写后没有 exec()；
 *  2) 视频线程需要在起播前就拿到稳定的设备指针读时钟，不能等本线程跑起来。
 */
class TAudioThread : public QThread
{
    Q_OBJECT
public:
    explicit TAudioThread(QObject *parent = nullptr);
    ~TAudioThread();

    //组装：解码器 + 音频包队列。同时创建并启动音频设备，
    //decode 为空（无音频流）时标记为不可用，但仍会排空队列
    virtual void init(TDecode *decode, TPacketQueue *queue, TMediaPlayCtrl *playCtrl);

    //单独送一帧。返回 false 表示线程已被中断
    virtual bool takeOne(AVFrame *frame);


    //音频输出设备（内含主时钟）。init() 之后一定非空
    TAudioIODevice *audioDevice() { return p_audioDev; }

    //获取AudioSink，实现音频设备控制
    QAudioSink *audioSink(){ return p_sink; }


    //音频是否不可用（无音频流/打开失败）。视频端据此退回按帧率定节拍
    bool isDisabled() const { return m_disabled; }



    void run() override;
private:
    TDecode *p_decode = nullptr;
    TPacketQueue *p_packetQueue = nullptr;
    TResample *p_sample = nullptr;
    QAudioSink *p_sink = nullptr;
    TAudioIODevice *p_audioDev = nullptr;
    TMediaPlayCtrl *p_playCtrl = nullptr;


    std::atomic<bool> m_disabled{false};


    bool pausedHandled = false;       //已对 sink 做过 suspend，避免重复



    //选择设备支持的 s16 输出格式
    QAudioFormat chooseFormat(const QAudioDevice &dev);
    //收净解码器当前所有帧,收到解码器帧就推入到队列，并推送data给sink
    void drainFrames();
    //EOF 收尾：排空解码器 + 重采样缓存，再等缓冲播完
    void drainTail();
    //音频不可用时排空包队列，避免解封装线程被背压卡死（表现为整片停住）
    void drainQueueOnly();
};

#endif // TAUDIOTHREAD_H
