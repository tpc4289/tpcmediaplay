#ifndef TDEMUXTHREAD_H
#define TDEMUXTHREAD_H

#include <QThread>

#include "tdemux.h"
#include "tmediaplayctrl.h"
#include "tpacketqueue.h"

class TDemuxThread : public QThread
{
    Q_OBJECT
public:
    explicit TDemuxThread(QObject *parent = nullptr);
    ~TDemuxThread();

    //解封装线程打开媒体文件方法
    virtual bool openMedia(const char *url);

    void run() override;
    //请求所有线程退出（先停消费者，再停自己）
    void stopAll();

    //封装提供m_demux对象访问
    TDemux *getTDemux();
    //封装提供videoPackQueue对象访问
    TPacketQueue *getVideoPackQueue();
    //封装提供audioPackQueue对象访问
    TPacketQueue *getAudioPackQueue();
    TMediaPlayCtrl *getMediaPlayCtrl();

    //视频位置跳转
    bool seekTo(double pos);

    //媒体总时长(ms)，直播流为 0
    int totalMs() const { return m_demux.getTotalMs(); }

    //暂停/继续（UI 线程调用，线程内轮询响应）
    void setPause(bool p) { playCtrl.paused = p; }
    bool isPaused() const { return playCtrl.paused; }

    //从TDemux中打开媒体文件的错误信息， openMedia 失败描述（界面提示用）
    QString openErrText() const { return m_demux.getOpenErrText(); }

private:
    //解封装对象
    TDemux m_demux;
    //视频包队列
    TPacketQueue videoPackQueue;
    //音频包队列
    TPacketQueue audioPackQueue;
    //播放器控制对象
    TMediaPlayCtrl playCtrl;

signals:
    //文件到尾部信号
    void endOfReached();


};

#endif // TDEMUXTHREAD_H
