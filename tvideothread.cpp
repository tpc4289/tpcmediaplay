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

    int cw = (w + 1)/2;
    int ch = (h + 1)/2;
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

        QByteArray yuv;
        bool ret = frameToYuv(frame, w, h, yuv);
        av_frame_free(&frame);
        if(!ret) continue;

        emit frameReady(yuv, w, h);

    }
}

bool TVideoThread::sync(int64_t pts)
{
    if(pts < 0)
        return true;
}

TVideoThread::TVideoThread(QObject *parent)
    : QThread{parent}
{}

TVideoThread::~TVideoThread()
{

}
//组装：解码器 + 视频包队列 + 播放控制块 + 帧率（无音频时算帧间隔）
void TVideoThread::init(TDecode *decode, TPacketQueue *queue, double fps)
{
    p_decode = decode;
    p_packetQueue = queue;
    frameDelayMs = (fps > 0) ? (1000.0 / fps) : 40.0;

}

//线程运行类
void TVideoThread::run()
{
    for(;;){
        TPacket *tp = nullptr;
        if(!p_packetQueue->pop(tp)){
            continue;
        }

        if(!tp) continue;

        //EOF 标志包：flush 解码器取净尾帧后退出
        if(!tp->pkt){
            delete tp;
            p_decode->flushDecoder();
            drainFrames();
            qDebug() << "TVideoDecode EOF done";
            break;
        }

        int ret = p_decode->send(tp->pkt);
        delete tp;

        msleep(40); //为了测试，先用40ms测试
        drainFrames();



    }
}
