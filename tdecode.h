#ifndef TDECODE_H
#define TDECODE_H

#include <mutex>

struct AVCodecParameters;
struct AVPacket;
struct AVRational;
struct AVFrame;
struct AVCodecContext;


class TDecode
{
private:
    std::mutex mux;
    bool isAudio = false;
    AVCodecContext *codecCtx = nullptr;
    AVPacket *pending = nullptr;   //EAGAIN 时缓存的包（克隆）
public:
    TDecode();
    ~TDecode();

    //打开解码器。不管成功与否都释放 para（所有权交给本函数，杜绝释放后使用/泄漏）
    //pktTimeBase：送入包的 pts 时基。TDemux 已把 pts 统一换算为毫秒，故默认 {1,1000}
    virtual bool open(AVCodecParameters *para);

    //送入解码器（本函数负责释放 pkt）。返回 0 已接收；AVERROR(EAGAIN) 解码器满，
    //包已内部克隆缓存，稍后调用 resend() 重发
    virtual int send(AVPacket *pkt);

    //重发因 EAGAIN 缓存的包。返回同 send
    virtual int resend();

    //尝试结束（flush 解码器）。返回 true 表示解码器已排空，可以开始 recv 收尾帧
    virtual bool flushDecoder();

    //获取解码帧。返回的 frame 由调用者 av_frame_free；无帧返回 nullptr
    virtual AVFrame *recv();

    //已打开的解码上下文（供重采样读取参数），未打开返回 nullptr
    AVCodecContext *context();

    void setIsAudio(bool isAudio);

    virtual void close();
    virtual void clear();



};

#endif // TDECODE_H
