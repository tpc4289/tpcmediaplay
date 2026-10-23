#include "tdecode.h"
#include <QDebug>
extern "C"{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

TDecode::TDecode() {}


//根据解码器参数，
bool TDecode::open(AVCodecParameters *para)
{
    if(!para) return false;
    //找到解码器
    const AVCodec *codec = avcodec_find_decoder(para->codec_id);
    if(!codec){
        qDebug()<<"avcodec_find_decoder failed! codec id is :"<<para->codec_id;
        //释放上下文参数
        //avcodec_parameters_free(&para);
        return false;
    }
    //根据解码器，分配解码器上下文空间
    AVCodecContext *tmpCtx = avcodec_alloc_context3(codec);
    if(!tmpCtx){
        qDebug()<<"avcodec_alloc_context3 failed!";
        return false;
    }
    //解码器参数到解码器上下文
    int ret = avcodec_parameters_to_context(tmpCtx, para);
    //解码器参数使用完，进行释放
    //avcodec_parameters_free(&para);
    if(ret < 0){
        qDebug()<<"avcodec_parameters_to_context failed";
        avcodec_free_context(&tmpCtx);
        return false;
    }
    //解码帧 pts 由 FFmpeg 按 pkt_timebase 换算后回填。
    //解封装层已把包 pts 统一换算为毫秒，所以时基传 {1,1000}，帧 pts 即毫秒
    tmpCtx->pkt_timebase = AVRational{1, 1000};
    //8线程解码
    tmpCtx->thread_count = 8;
    //打开解码器上下文：第二参传 codec
    ret = avcodec_open2(tmpCtx, codec, nullptr);
    if(ret < 0){
        char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, buf, sizeof(buf) - 1);
        qDebug()<<"avcodec_open2 failed: " << buf;
        avcodec_free_context(&tmpCtx);
        mux.unlock();
        return false;
    }
    mux.lock();
    codecCtx = tmpCtx;
    mux.unlock();
    qDebug()<<"avcodec open success!";

    return true;
}
//送入packet解码
int TDecode::send(AVPacket *pkt)
{
    mux.lock();
    if(!codecCtx){
        mux.unlock();
        if(pkt)
            av_packet_free(&pkt);
        return -1;
    }
    int ret = 0;
    if(pkt){
        ret = avcodec_send_packet(codecCtx, pkt);
        if(ret == AVERROR(EAGAIN)){
            //解码器满：克隆留存，等消费者取帧后 resend
            if (!pending)
                pending = av_packet_clone(pkt);
        }else if(ret < 0){
            char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, buf, sizeof(buf) - 1);
            qDebug()<<"send error:"<<buf;
        }
        //无论成败，包都由本函数释放
        av_packet_free(&pkt);
    }
    mux.unlock();
    return ret;
}

//封装函数，不让外面直接调用设置
void TDecode::setIsAudio(bool isAud)
{
    this->isAudio = isAud;
}

void TDecode::close()
{
    mux.lock();
    if(pending)
        av_packet_free(&pending);
    if(codecCtx)
        avcodec_free_context(&codecCtx);
    mux.unlock();
}
