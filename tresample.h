#ifndef TRESAMPLE_H
#define TRESAMPLE_H

struct AVFrame;
struct SwrContext;
struct AVCodecContext;
class TResample
{
private:
    SwrContext *swrCtx = nullptr;
    AVCodecContext *srcCtx = nullptr;
    //速率
    int rate = 0;
    //声道
    int channels = 0;
public:
    TResample();
    ~TResample();

    //src：解码上下文（取 sample_rate / ch_layout / sample_fmt）
    //dstRate、dstChannels：声卡目标格式参数
    bool open(AVCodecContext *src, int dstRate, int dstChannels);
    //重采样一帧。返回调用者持有的新 AVFrame（s16 交织，av_frame_free 释放）；失败返回 nullptr
    AVFrame *resample(AVFrame *frame);
    //流结束时调用，拿缓存里剩下的音频
    AVFrame *flush();
    //释放空间
    void close();
    //seek 时调用：丢弃重采样器内部缓存的延迟样本（旧位置的音频尾巴），
    //否则跳转后还会先播出旧位置的声音，造成拖拽延迟感
    void reset();
};

#endif // TRESAMPLE_H
