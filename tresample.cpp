#include "tresample.h"
#include <qDebug>
extern "C"{
#include <libswresample/swresample.h>
#include <libavcodec/avcodec.h>
}

TResample::TResample() {}


//src：解码上下文（取 sample_rate / ch_layout / sample_fmt）
//dstRate、dstChannels：声卡目标格式参数
bool TResample::open(AVCodecContext *src, int dstRate, int dstChannels)
{
    //关闭存在的上下文
    close();
    if(!src || src->sample_rate <= 0 || dstRate <=0 || dstChannels <= 0){
        qDebug()<<"TResample open failed: bad AVCodecContext!";
        return false;
    }
    //赋值给内部对象
    srcCtx = src;
    rate = dstRate;
    channels = dstChannels;

    //新版函数FFmpeg 6+：swr_alloc_set_opts2 + AVChannelLayout
    AVChannelLayout srcChLayout = src->ch_layout, dstChLayout;
    //根据目标通道数设置通道平面
    av_channel_layout_default(&dstChLayout, dstChannels);
    //**创建 / 配置 SwrContext 的函数**，**使用 `AVChannelLayout`（结构体）**
    int ret = swr_alloc_set_opts2(&swrCtx, &dstChLayout, AV_SAMPLE_FMT_S16, dstRate, &srcChLayout, (AVSampleFormat)src->sample_fmt, src->sample_rate, 0, nullptr);
    if(ret < 0 || !swrCtx){
        qDebug()<<"swr_alloc_set_opts2 failed!";
        return false;
    }
    //初始化
    ret = swr_init(swrCtx);
    if(ret < 0){
        char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, buf, sizeof(buf) - 1);
        qDebug() << "swr_init failed:" << buf;
        swr_free(&swrCtx);
        return false;
    }

    qDebug() << "TResample open ok:" << src->sample_rate << "->" << dstRate
             << "ch" << srcChLayout.nb_channels << "->" << dstChannels;
    return true;
}

//重采样一帧。返回调用者持有的新 AVFrame（s16 交织，av_frame_free 释放）；失败返回 nullptr
AVFrame *TResample::resample(AVFrame *frame)
{
    //如果上下文为空，或者frame为空，或者nb_samples不对，返回空
    if(!swrCtx || !frame || frame->nb_samples <= 0)
        return nullptr;

    //先查询本次最多需要多少输出采样，避免buffer不够越界
    int maxOutSamples = swr_get_out_samples(swrCtx, frame->nb_samples);
    if(maxOutSamples <= 0)
        return nullptr;
    //目标帧：nb_samples 帧 * 通道 * 2 字节(s16)，由 av_frame_get_buffer 分配
    int destNb = frame->nb_samples;
    //分配目标帧空间
    AVFrame *outFrame = av_frame_alloc();
    outFrame->format = AV_SAMPLE_FMT_S16;
    outFrame->sample_rate = rate;
    //**根据声道数量，填充一个默认声道布局到 `AVChannelLayout` 结构体**FFmpeg `ch_layout`：出参，要初始化的声道布局结构体 `nb_channels`：声道数（1、2、6 等）
    av_channel_layout_default(&outFrame->ch_layout, channels);
    //outFrame->nb_samples = frame->nb_samples;按最大预估采样数分配buffer
    outFrame->nb_samples = maxOutSamples;
    //给 AVFrame 分配视频 / 音频数据缓冲区（带引用计数的 AVBuffer）- `frame`：已经填好格式信息的 AVFrame
    //- `align`：内存对齐字节；**传 0 让 FFmpeg 自动选对齐（推荐）**，常见手动填 32、64 用于 SIMD 优化
    int ret = av_frame_get_buffer(outFrame, 0);
    if(ret < 0){
        qDebug()<<"av_frame_get_buffer failed!";
        av_frame_free(&outFrame);
        return nullptr;
    }

    //原始帧的第一个平面的数据
    const uint8_t *srcData[1] = {frame->data[0]};
    //目标帧的第一个平面的数据
    uint8_t *destData[1] = {outFrame->data[0]};

    //音频重采样函数，**成功返回本次转换得到的采样点数（per channel）**；负数 = AVERROR 错误
    ret = swr_convert(swrCtx, destData, destNb,srcData, frame->nb_samples);
    //新版采样函数swr_convert_frame(SwrContext *swr, AVFrame *output, const AVFrame *input);
    if(ret < 0){
        char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, buf, sizeof(buf) - 1);
        qDebug() << "swr_convert failed:" << buf;
        av_frame_free(&outFrame);
        return nullptr;
    }
    outFrame->nb_samples = ret;
    outFrame->pts = frame->pts;
    return outFrame;
}
//流结束时调用，拿缓存里剩下的音频
AVFrame *TResample::flush()
{
    if(!swrCtx) return nullptr;
    int maxOutSamples = swr_get_out_samples(swrCtx, 0);
    if(maxOutSamples <= 0) return nullptr;

    AVFrame *outFrame = av_frame_alloc();
    outFrame->format = AV_SAMPLE_FMT_S16;
    outFrame->sample_rate = rate;
    av_channel_layout_default(&outFrame->ch_layout, channels);
    outFrame->nb_samples = maxOutSamples;

    int ret = av_frame_get_buffer(outFrame, 0);
    if(ret < 0){
        av_frame_free(&outFrame);
        return nullptr;
    }
    //目标帧的第一个平面的数据
    uint8_t *destData[1] = {outFrame->data[0]};
    ret = swr_convert(swrCtx, destData, maxOutSamples, nullptr, 0);
    if(ret <= 0){
        av_frame_free(&outFrame);
        return nullptr;
    }
    outFrame->nb_samples = ret;
    outFrame->pts = AV_NOPTS_VALUE;
    return outFrame;
}

//释放swr上下文对象
void TResample::close()
{
    if(swrCtx){
        swr_free(&swrCtx);
        swrCtx = nullptr;
    }
    swrCtx = nullptr;
}
