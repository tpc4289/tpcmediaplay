#include "tdemuxthread.h"
#include <QDebug>

extern "C"{
#include <libavcodec/avcodec.h>
}

TDemuxThread::TDemuxThread(QObject *parent)
    : QThread{parent}
{}

TDemuxThread::~TDemuxThread()
{

}

bool TDemuxThread::openMedia(const char *url)
{
    //也可把 open 挪进 run() 彻底不卡界面）
    return m_demux.open(url);
}

void TDemuxThread::run()
{
    qDebug() << "TDemuxThread start";
    for(;;){
        //获取视频文件包
        AVPacket *pack = m_demux.read();
        //如果获取为空
        if(!pack){
            //首先判断是否是因为到文件尾部了
            if(m_demux.getIsEndOf()){
                qDebug() << "TDemuxThread EOF";
                //向两条队列各压入一个 EOF 标志包（pkt==nullptr）
                auto pushEof = [&](TPacketQueue *queue) {
                    TPacket *tp = new TPacket();   //pkt=nullptr, pts=-1
                    std::unique_lock<std::mutex> lk(queue->mux);
                    queue->push(tp, lk);
                };
                //视频和音频队列插入一个尾部
                pushEof(&videoPackQueue);
                pushEof(&audioPackQueue);
                //发送尾部信号
                emit endOfReached();
                break;
            }
            //网络抖动 / rtsp 瞬时无数据：稍候重试
            msleep(5);
            continue;
        }
        //判断是视频包还是音频包
        TPacketQueue *queue = m_demux.isAudio(pack) ? &audioPackQueue : &videoPackQueue;
        TPacket *tp = new TPacket();
        ////所有权随包转移进队列
        tp->pkt = pack;
        tp->pts = pack->pts;
        std::unique_lock<std::mutex> lk(queue->mux);
        queue->push(tp, lk);         //满时在此 wait，形成背压
    }

    //收尾：确保消费者被唤醒退出
    videoPackQueue.stop();
    audioPackQueue.stop();
    qDebug() << "TDemuxThread end";
}
