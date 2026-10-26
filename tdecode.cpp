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
        avcodec_parameters_free(&para);
        return false;
    }
    mux.lock();
    //根据解码器，分配解码器上下文空间
    codecCtx = avcodec_alloc_context3(codec);
    if(!codecCtx){
        qDebug()<<"avcodec_alloc_context3 failed!";
        //释放上下文参数
        avcodec_parameters_free(&para);
        mux.unlock();
        return false;
    }
    //解码器参数到解码器上下文
    int ret = avcodec_parameters_to_context(codecCtx, para);
    //解码器参数使用完，进行释放
    avcodec_parameters_free(&para);
    if(ret < 0){
        qDebug()<<"avcodec_parameters_to_context failed";
        avcodec_free_context(&codecCtx);
        codecCtx  = nullptr;
        mux.unlock();
        return false;
    }
    //解码帧 pts 由 FFmpeg 按 pkt_timebase 换算后回填。
    //解封装层已把包 pts 统一换算为毫秒，所以时基传 {1,1000}，帧 pts 即毫秒
    codecCtx->pkt_timebase = AVRational{1, 1000};
    //8线程解码
    codecCtx->thread_count = 8;
    //打开解码器上下文：第二参传 codec
    ret = avcodec_open2(codecCtx, codec, nullptr);
    if(ret < 0){
        char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, buf, sizeof(buf) - 1);
        qDebug()<<"avcodec_open2 failed: " << buf;
        avcodec_free_context(&codecCtx);
        codecCtx  = nullptr;
        mux.unlock();
        return false;
    }


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
            if (!pending){
                pending = av_packet_clone(pkt);
                qDebug()<<"decoder EAGAIN, cache pending pkt";
            }else
                qDebug()<<"decoder EAGAIN, pending alreary exist, drop pkt";
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
//重试用pending包
// 返回：0成功；AVERROR(EAGAIN) 继续满，pending保留；<0 失败，pending已释放
int TDecode::resend()
{
    mux.lock();;
    if(!codecCtx || !pending){
        mux.unlock();
        return 0;
    }
    int ret = avcodec_send_packet(codecCtx, pending);
    if(ret == 0){
        av_packet_free(&pending);
        pending = nullptr;
    }else if(ret != AVERROR(EAGAIN)){
        //真实错误，丢弃pending
        char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, buf, sizeof(buf) - 1);
        qDebug()<<"avcodec_open2 failed: " << buf;
        av_packet_free(&pending);
        pending = nullptr;
    }
    mux.unlock();
    // ret == EAGAIN：不释放pending，返回EAGAIN，上层下次再试
    return ret;
}

// 冲刷解码器，准备取完剩余帧
// 返回 true：冲刷发起完成；false：解码器满，先去recv

bool TDecode::flushDecoder()
{
    mux.lock();
    if(!codecCtx){
        mux.unlock();
        return true;
    }
    if(pending){
        int ret = avcodec_send_packet(codecCtx, pending);
        if(ret == AVERROR(EAGAIN)){
            mux.unlock();
            return false;
        }
        // 成功或者其他错误，都释放pending
        av_packet_free(&pending);
        pending = nullptr;
    }
    avcodec_send_packet(codecCtx, nullptr);
    mux.unlock();
    return true;
}

//调用方用完frame,必须av_frame_free;
AVFrame *TDecode::recv()
{
    mux.lock();
    if(!codecCtx){
        mux.unlock();
        return nullptr;
    }
    //分配空间
    AVFrame *frame = av_frame_alloc();
    int ret = avcodec_receive_frame(codecCtx, frame);
    mux.unlock();
    if(ret != 0){
        qDebug()<<"avcodec_receive_frame failed!";
        av_frame_free(&frame);
        return nullptr;
    }
    qDebug()<<"avcodec_receive_frame success!";
    return frame;
}

//seek后调用，清理pending + 刷新解码器缓存
void TDecode::clear()
{
    mux.lock();
    if(pending){
        av_packet_free(&pending);
        pending = nullptr;
    }
    if(codecCtx){
        avcodec_flush_buffers(codecCtx);
    }
    mux.unlock();
}

AVCodecContext *TDecode::codecContext()
{
    mux.lock();
    AVCodecContext *ctx = codecCtx;
    mux.unlock();
    return ctx;
}

//封装函数，不让外面直接调用设置
void TDecode::setIsAudio(bool isAud)
{
    this->isAudio = isAud;
}

void TDecode::close()
{
    mux.lock();
    if(pending){
        av_packet_free(&pending);
        pending = nullptr;
    }
    if(codecCtx){
        avcodec_free_context(&codecCtx);
        codecCtx = nullptr;
    }
    mux.unlock();
}
