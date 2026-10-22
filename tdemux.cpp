#include "tdemux.h"
#include <QDebug>
extern "C"{
#include "libavformat/avformat.h"
}

//把ffmpeg内部的分数转换为浮点数
static double r2d(AVRational r) {
    return r.den == 0 ? 0 : (double)r.num / (double)r.den;
}

TDemux::TDemux() {
#if LIBAVFORMAT_VERSION_MAJOR < 58
        //初始化封装库（FFmpeg 5 起已删除，用版本宏兜底）
        av_register_all();
#endif
        //初始化网络库（可以打开 rtsp rtmp http 协议的流媒体视频）
        avformat_network_init();
}

TDemux::~TDemux()
{
    //对象销毁时候，关闭释放解封装上下文对象
    close();
}


//解封装输入视频文件信息，获取到视频流和音频流信息
bool TDemux::open(const char *url)
{
    //参数设置的字典
    AVDictionary *dic = nullptr;
    //设置rtsp流以tcp协议打开
    av_dict_set(&dic, "rtsp_transport", "tcp", 0);
    //网络延时（单位微秒）
    av_dict_set(&dic, "max_delay", "600000", 0);

    //多线程模式，设置互斥锁
    mux.lock();
    //重复打开时先释放旧上下文
    if(p_formatCtx)
        avformat_close_input(&p_formatCtx);
    //**打开媒体输入、读取文件头**的核心解封装函数，**只读容器头，不会打开解码器**FFmpeg
    int ret = avformat_open_input(&p_formatCtx, url, NULL, &dic);
    //返回值小于0，说明打开失败，出现报错
    if(ret < 0){
        mux.unlock();
        char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        //输出报错
        av_strerror(ret, buf, sizeof(buf)-1);
        qDebug()<<"open "<<url<<" failed: "<<buf;
        return false;
    }
    qDebug()<<"open "<<url<<" success!";

    //读取视频流信息:很多 mp4 不开这一步拿不到完整 codecpar / 帧率 读若干 packet，补齐音视频流参数
    ret = avformat_find_stream_info(p_formatCtx, NULL);
    //返回值小于0，说明读取流信息失败，出现报错
    if(ret < 0){
        mux.unlock();
        char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        //输出报错
        av_strerror(ret, buf, sizeof(buf)-1);
        qDebug()<<"avformat_find_stream_info error: "<<buf;
        avformat_close_input(&p_formatCtx);
        return false;
    }
    //打印视频流详细信息,这一步不是必须的
    av_dump_format(p_formatCtx, 0, url, 0);
    qDebug()<<"formatCtx.duration is： "<<p_formatCtx->duration;

    //**在 AVFormatContext 里自动挑选最合适的一路流**（视频 / 音频 / 字幕），返回 stream index；属于 libavformat，必须在 `avformat_find_stream_info` 之后调用FFmpeg
    //音频流
    audio_idx = av_find_best_stream(p_formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    //没有音频流，我们只输出提示！
    if(audio_idx < 0){
        qDebug()<<"no audio stream!";
    }
    video_idx = av_find_best_stream(p_formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    //没有视频流，我们输出提示，并返回false！
    if(video_idx < 0){
        avformat_close_input(&p_formatCtx);
        mux.unlock();
        qDebug()<<"no video stream!";
        return false;
    }
    //视频流信息
    AVStream *videoStream = p_formatCtx->streams[video_idx];
    //通过流参数信息获取视频宽
    v_width = videoStream->codecpar->width;
    //通过流参数信息获取视频高
    v_height = videoStream->codecpar->height;
    //获取和计算帧率
    v_fps = r2d(videoStream->avg_frame_rate);
    //视频总时长,v_totalMs初始值为0
    if(p_formatCtx->duration > 0){
        v_totalMs = int(m_formatCtx->duration * 1000 / AV_TIME_BASE);
    }

    qDebug()<<"videoStream.duration is： "<<videoStream->duration;
    //如果视频大小有问题，我们也退出
    if(v_width <=0 || v_height <=0){
        avformat_close_input(&p_formatCtx);
        mux.unlock();
        qDebug()<<"video have bad size!";
        return false;
    }
    mux.unlock();
    //输出一下我们的视频信息
    qDebug()<<"=========video information===========";
    qDebug()<<"video  width: "<<v_width;
    qDebug()<<"video height: "<<v_width;
    qDebug()<<"video    fps: "<<v_fps;
    return true;
}


void TDemux::close()
{
    //多线程情况下，还是提供互斥锁
    mux.lock();
    if(!p_formatCtx){
        mux.unlock();
        return;
    }
    //释放解封装的上下文对象
    avformat_close_input(&p_formatCtx);
    v_totalMs = 0;
    mux.unlock();
}
