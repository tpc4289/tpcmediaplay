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
    //退出前确保读写双方都被唤醒并收尾，避免析构后仍有线程访问成员
    videoPackQueue.stop();
    audioPackQueue.stop();
    if(isRunning()){
        requestInterruption();
        wait();
    }
}

bool TDemuxThread::openMedia(const char *url)
{
    //也可把 open 挪进 run() 彻底不卡界面）
    return m_demux.open(url);
}

void TDemuxThread::run()
{
    qDebug() << "TDemuxThread start";
    bool hasAudio = m_demux.hasAudio();
    for(;;){
        if(isInterruptionRequested())
            break;

        //处理 seek：在本线程执行，避免与其他线程并发操作解封装上下文
        int seekP = playCtrl.seekPermille.exchange(-1);
        if(seekP >= 0){
            double pos = seekP/1000.0;
            m_demux.seek(pos);
            videoPackQueue.flush();        //丢弃旧位置的未消费包
            audioPackQueue.flush();
        }

        //暂停：不读新包，队列水位保持不变
        if (playCtrl.paused) {
            msleep(10);
            continue;
        }


        //获取视频文件包
        AVPacket *pack = m_demux.read();
        //如果获取为空
        if(!pack){
            //首先判断是否是因为到文件尾部了
            if(m_demux.getIsEndOf()){
                qDebug() << "TDemuxThread EOF";
                //向两条队列各压入一个 EOF 标志包（pkt==nullptr） ,type用来传入辨别为视频队列还是音频队列
                auto pushEof = [&](TPacketQueue *queue, int type) {
                    TPacket *tp = new TPacket();   //pkt=nullptr, pts=-1
                    std::unique_lock<std::mutex> lk(queue->mux);
                    queue->push(tp, type, lk);
                };
                //视频和音频队列插入一个尾部
                pushEof(&videoPackQueue, 0);
                //无音频流时不必给音频队列发 EOF：没人消费会一直卡在 push 的背压等待
                if(hasAudio)
                    pushEof(&audioPackQueue, 1);
                //发送尾部信号
                emit endOfReached();
                break;
            }
            //网络抖动 / rtsp 瞬时无数据：稍候重试
            msleep(5);
            continue;
        }
        //判断是视频包还是音频包
        //修改为精准判断
        TPacketQueue *queue = nullptr;
        int type = 0;
        if(m_demux.isVideo(pack)){
            queue = &videoPackQueue;
        }else if(m_demux.isAudio(pack)){
            queue = &audioPackQueue;
            type = 1;
        }
        else{
            //字幕等其它流：直接丢弃，不进任何解码队列
            av_packet_free(&pack);
            continue;
        }
        TPacket *tp = new TPacket();
        //所有权随包转移进队列
        tp->pkt = pack;
        tp->pts = pack->pts;
        std::unique_lock<std::mutex> lk(queue->mux);
        queue->push(tp, type, lk);         //满时在此 wait，形成背压
    }

    //收尾：确保消费者被唤醒退出
    videoPackQueue.stop();
    audioPackQueue.stop();
    qDebug() << "TDemuxThread end";
}

void TDemuxThread::stopAll()
{
    playCtrl.stopRequest = true;
    playCtrl.paused = false; //暂停中退出时先放开
    videoPackQueue.stop();
    audioPackQueue.stop();
    if(isRunning()){
        requestInterruption();
        quit();
        wait(3000);
    }

}

TDemux *TDemuxThread::getTDemux(){return &m_demux;}

TPacketQueue *TDemuxThread::getVideoPackQueue(){return &videoPackQueue;}

TPacketQueue *TDemuxThread::getAudioPackQueue(){return &audioPackQueue;}

TMediaPlayCtrl *TDemuxThread::getMediaPlayCtrl()
{
    return &playCtrl;
}

//视频位置跳转
bool TDemuxThread::seekTo(double pos)
{
    if(m_demux.getTotalMs() <= 0){
        qDebug() << "seek ignored: live stream or unknown duration";
        return false;
    }
    int p = int(pos*1000.0);
    if(p < 0) p = 0;
    if(p > 1000) p = 1000;
    playCtrl.seekPermille = p;
    return true;
}
