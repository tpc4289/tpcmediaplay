#ifndef TDEMUX_H
#define TDEMUX_H
#include <QMutex>

struct AVFormatContext;

class TDemux
{
private:
    QMutex mux;
    //用来存放FFMPEG解封装后的上下文，该内部信息通过ffmpeg解封装函数avformat_open_input填入
    AVFormatContext *p_formatCtx = nullptr;
    //视频流对应索引
    int video_idx = -1;
    //音频流对应索引
    int audio_idx = -1;
    //视频宽度
    int v_width = 0;
    //视频高度
    int v_height = 0;
    //帧率
    int v_fps = 0;
    //视频总时长-毫秒
    int v_totalMs = 0;
public:
    TDemux();
    ~TDemux();


    //打开媒体文件，或者流媒体 rtmp http rtsp
    virtual bool open(const char *url);
    //有打开就有关闭，主要是关闭解封装后的上下文对象
    virtual void close();

};

#endif // TDEMUX_H
