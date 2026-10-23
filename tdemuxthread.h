#ifndef TDEMUXTHREAD_H
#define TDEMUXTHREAD_H

#include <QThread>

#include "tdemux.h"
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

    //封装提供m_demux对象访问
    TDemux *getTDemux(){return &m_demux;};
    //封装提供videoPackQueue对象访问
    TPacketQueue *getVideoPackQueue(){return &videoPackQueue;};
    //封装提供audioPackQueue对象访问
    TPacketQueue *getAudioPackQueue(){return &audioPackQueue;};

private:
    //解封装对象
    TDemux m_demux;
    //视频包队列
    TPacketQueue videoPackQueue;
    //音频包队列
    TPacketQueue audioPackQueue;

signals:
    //文件到尾部信号
    void endOfReached();


};

#endif // TDEMUXTHREAD_H
