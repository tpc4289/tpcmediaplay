#include "tvideothread.h"
#include "tdecode.h"
#include "tpacketqueue.h"
#include <QDebug>

extern "C"{
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

//解码帧 -> YUV420P，打包进 out（y + u + v 连续）
bool TVideoThread::frameToYuv(AVFrame *frame, int w, int h, QByteArray &out)
{
    //宽或高不合法，直接返回
    if(w <=0 || h <= 0)
        return false;

    // int cw = (w + 1)/2;
    // int ch = (h + 1)/2;
    int cw = w /2, ch = h / 2;
    //YUV中的Y大小
    int ySize = w * h;
    //YUV中的U和V大小
    int uvSize = cw * ch;
    //实际大小
    out.resize(ySize + uvSize * 2);
    uint8_t *dst = (uint8_t *)out.data();

    if(!swsCtx || swsFmt != frame->format || swsWidth != w || swsHeight != h){
        if(swsCtx) sws_freeContext(swsCtx);
        swsCtx = sws_getContext(w, h, (AVPixelFormat)frame->format, w, h, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
        swsFmt = frame->format, swsWidth = w, swsHeight = h;
        if(!swsCtx){
            qDebug()<<"sws_getContext failed, fmt="<<frame->format;
            return false;
        }

    }

    uint8_t *dstData[4] = {dst, dst+ySize, dst+ySize+uvSize, nullptr};
    int dstStride[4] = {w, cw, cw, 0};

    int ret = sws_scale(swsCtx, (const uint8_t * const *)frame->data, frame->linesize, 0, h, dstData, dstStride);

    if(ret <= 0){
        qDebug() << "sws_scale failed: ret="<<ret;
        return false;
    }
    return true;

}


//无音频/时钟未就绪时：按帧率定节拍，避免解码多快就显示多快
void TVideoThread::paceByFrameRate()
{
    if(!paceTimer.isValid())
    {
        paceTimer.start();
        return;
    }
    qint64 elapsed = paceTimer.elapsed();
    if(elapsed < qint64(frameDelayMs)){
        msleep(uint(frameDelayMs - elapsed));
    }
    paceTimer.restart();
}

//取空解码器当前所有帧，逐帧同步并送显
void TVideoThread::drainFrames()
{
    for(;;){
        AVFrame *frame = p_decode->recv();
        if(!frame)
            break;
        int w = frame->width;
        int h = frame->height;
        int64_t pts = frame->pts;//解码器已按 pkt_timebase{1,1000} 回填为毫秒

        //先同步再转码：决定丢帧时省掉一次 sws_scale 与一次 QByteArray 拷贝
        bool keep = sync(pts);
        if(!keep){
            av_frame_free(&frame);
            continue;
        }

        QByteArray yuv;
        bool ret = frameToYuv(frame, w, h, yuv);
        av_frame_free(&frame);
        if(!ret) continue;

        emit frameReady(yuv, w, h);

    }
}

//音画同步：睡到音频时钟追上本帧 pts。返回 false 表示该帧严重落后，应丢弃
bool TVideoThread::sync(int64_t pts)
{
    if(pts < 0){
        //pts 不可用（个别流没给）：退化为按帧率播
        paceByFrameRate();
        return true;
    }

    int64_t clock = p_audioDev ? p_audioDev->getClock() : CLOCK_NOT_READY;

    //时钟还没就绪（起播头几帧/音频被禁用）：按帧率定节拍
    if(clock == CLOCK_NOT_READY){
        paceByFrameRate();
        return true;
    }

    int64_t diff = pts - clock;   // >0 视频超前，需要等；<0 视频落后，考虑丢帧

    if(diff < SYNC_DROP_MS){
        //落后音频太多：这一帧显示出来也只是闪一下，丢掉
        qDebug() << "video drop frame, pts="<<pts<<" clock="<<clock<<" diff="<<diff;
        return false;
    }

    if(diff > SYNC_SLEEP_MS){
        //超前：睡到音频追上。加单帧等待上限，防止音频时钟异常时画面完全冻住
        int64_t waitMs = diff;
        if(waitMs > SYNC_MAX_WAIT_MS)
            waitMs = SYNC_MAX_WAIT_MS;
        msleep(uint(waitMs));
    }
    //diff 在 [SYNC_DROP_MS, SYNC_SLEEP_MS] 之间：误差可接受，直接显示

    //时钟停滞检测：连续多帧读到同一个时钟值，说明音频端没在消费
    //（设备异常/被禁用），此时继续等只会把画面拖成慢动作，改为按帧率自走
    if(clock == lastClock)
        ++clockStallCount;
    else
        clockStallCount = 0;
    lastClock = clock;
    if(clockStallCount > 20){
        paceByFrameRate();
        clockStallCount = 0;
    }
    return true;
}

TVideoThread::TVideoThread(QObject *parent)
    : QThread{parent}
{}

TVideoThread::~TVideoThread()
{
    //先停线程再释放资源：本线程还在从 p_packetQueue 取包，
    //若队列随解封装线程先析构，就是访问已释放对象
    requestInterruption();
    wait();

    if(swsCtx){
        sws_freeContext(swsCtx);
        swsCtx = nullptr;
    }
}
//组装：解码器 + 视频包队列 + 播放控制块 + 帧率（无音频时算帧间隔）
void TVideoThread::init(TDecode *decode, TPacketQueue *queue, double fps, TAudioIODevice *audioDev, TMediaPlayCtrl *playCtrl)
{
    p_decode = decode;
    p_packetQueue = queue;
    p_audioDev = audioDev;
    p_playCtrl = playCtrl;
    frameDelayMs = (fps > 0) ? (1000.0 / fps) : 40.0;

}

//线程运行类
void TVideoThread::run()
{
    for(;;){

        if(p_playCtrl->stopRequest || isInterruptionRequested())
            break;

        //seek/重播 清理解码缓冲
        if (p_playCtrl->takeFlush(&TMediaPlayCtrl::flushVideo)) {
            p_decode->clear();
        }


        TPacket *tp = nullptr;
        if(!p_packetQueue->pop(tp)){
            //pop 返回 false = 队列已停止（播完/关窗），必须退出。
            //原来写 continue：队列 stop 后 pop 恒为 false，这里变成忙等死循环，
            //关窗时线程永远 wait 不回来，程序卡死。
            break;
        }

        if(!tp) continue;

        //EOF 标志包：flush 解码器取净尾帧后退出
        if(!tp->pkt){
            delete tp;
            //与音频线程同理：解码器仍满时要反复取帧再 flush
            for(int i = 0; i < 8; ++i){
                if(p_decode->flushDecoder())
                    break;
                drainFrames();
            }
            drainFrames();
            qDebug() << "TVideoDecode EOF done";
            break;
        }

        int ret = p_decode->send(tp->pkt);
        delete tp;

        if(ret < 0){
            if(ret == AVERROR(EAGAIN)){
                //解码器满：先取帧腾空间，再重发缓存的 pending 包。
                //原实现直接 continue 会把包丢掉，画面会跳
                drainFrames();
                while(p_decode->resend() == AVERROR(EAGAIN) && !isInterruptionRequested())
                    drainFrames();
            }
            continue;
        }
        drainFrames();



    }
}
