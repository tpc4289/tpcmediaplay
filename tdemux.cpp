#include "tdemux.h"
#include <QDebug>
extern "C"{
#include <libavformat/avformat.h>
#include <libavutil/display.h>
}

//把ffmpeg内部的分数转换为浮点数
static double r2d(AVRational r) {
    return r.den == 0 ? 0.0 : (double)r.num / (double)r.den;
}
//把以流 time_base 计的时间戳换算成毫秒；AV_NOPTS_VALUE 返回 -1
static qint64 toMs(qint64 ts, AVRational ration){
    if(ts == AV_NOPTS_VALUE || ration.num == 0 || ration.den == 0)
        return -1;
    static const AVRational ms = {1, 1000};
    return av_rescale_q(ts, ration, ms);
}


//从视频流提取旋转元数据：displaymatrix 边数据（AV_PKT_DATA_DISPLAYMATRIX）。
//注意版本差异：FFmpeg 7 及以下挂在 AVStream 上（av_stream_get_side_data），
//本工程的 FFmpeg 8（libavformat 63）把它移到了 AVCodecParameters->coded_side_data，
//且 av_stream_get_side_data 已删除——按旧字段写会编译失败。
//老文件还可能只有 metadata 的 "rotate" 标签，作为回退。
//返回"显示所需逆时针旋转角度"（0/90/180/270）
static int streamRotateCCWDegrees(AVStream *st)
{
    if(!st || !st->codecpar)
        return 0;
    double theta = 0;
    bool found = false;
    for(int i = 0; i < st->codecpar->nb_coded_side_data; i++){
        const AVPacketSideData *sd = &st->codecpar->coded_side_data[i];
        if(sd->type == AV_PKT_DATA_DISPLAYMATRIX && sd->size >= 9 * (int)sizeof(int32_t)){
            theta = av_display_rotation_get((const int32_t *)sd->data);
            found = true;
            break;
        }
    }
    if(!found){
        //回退：旧 mp4/mov 的 "rotate" metadata 标签（值即逆时针角度，可能是多位数）
        AVDictionaryEntry *e = av_dict_get(st->metadata, "rotate", NULL, 0);
        if(e){
            bool ok = false;
            theta = QString::fromUtf8(e->value).toDouble(&ok);
            if(!ok) return 0;
        }
        else
            return 0;
    }
    if(qIsNaN(theta))
        return 0;
    theta -= 360 * floor(theta / 360 + 0.9 / 360);
    int deg = qRound(theta / 90.0) * 90;      //归一到 90° 整数倍
    deg %= 360;
    if(deg < 0)
        deg += 360;
    return deg;
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
    TDemux::close();
}


//是否有音频流
//与 getFps/getIsEndOf 同样不加锁：audio_idx 只在 open/close 中变更，
//而 open 发生在解封装线程启动之前（有 happens-before），run() 里读取是安全的
bool TDemux::hasAudio() const
{
    return (audio_idx >= 0);
}


//解封装输入视频文件信息，获取到视频流和音频流信息
bool TDemux::open(const char *url)
{
    //打开之前，先把一些数据复位。
    v_openErr = OE_OK;
    v_openErrDetail.clear();
    v_turnWidth = 0;
    v_turnHeight = 0;
    v_rotate = 0;
    v_colorRange = 0;
    v_is709 = false;

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
    //返回值小于0，说明打开失败，文件根本不是支持的媒体文件
    if(ret < 0){
        mux.unlock();
        char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        v_openErr = OE_OPEN_FAILED; //不支持的媒体文件;
        //输出报错
        av_strerror(ret, buf, sizeof(buf)-1);
        v_openErrDetail = QString::fromLatin1(buf);
        qDebug()<<"open "<<url<<" failed: "<<buf;
        return false;
    }
    qDebug()<<"open "<<url<<" success!";

    //读取流信息:很多 mp4 不开这一步拿不到完整 codecpar / 帧率 读若干 packet，补齐音视频流参数
    ret = avformat_find_stream_info(p_formatCtx, NULL);
    //返回值小于0，说明读取流信息失败，出现报错
    if(ret < 0){
        avformat_close_input(&p_formatCtx);
        mux.unlock();
        v_openErr = OE_NO_STREAM_INFO; //流信息探测失败：文件损坏、截断或伪造扩展名。
        char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        //输出报错
        av_strerror(ret, buf, sizeof(buf)-1);
        v_openErrDetail = QString::fromLatin1(buf);
        qDebug()<<"avformat_find_stream_info error: "<<buf;
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
        v_openErr = OE_NO_VIDEO; //容器能够打开但没有视频流（典型：纯音频 mp3/flac）——单独分类便于界面提示
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
    //获取和计算帧率（保留小数：23.976 截断会让无音频兜底定拍偏慢；
    v_fps = r2d(videoStream->avg_frame_rate);
    //VBR 的 MKV 常见 avg_frame_rate 为 0，回退 r_frame_rate）
    if(v_fps <= 0)
        v_fps = r2d(videoStream->r_frame_rate);
    //视频总时长,v_totalMs初始值为0
    if(p_formatCtx->duration > 0){
        v_totalMs = int(p_formatCtx->duration * 1000 / AV_TIME_BASE);
    }

    qDebug()<<"videoStream.duration is： "<<videoStream->duration;
    //如果视频大小有问题，我们也退出
    if(v_width <=0 || v_height <=0){
        v_openErr = OE_BAD_SIZE;
        avformat_close_input(&p_formatCtx);
        mux.unlock();
        qDebug()<<"video have bad size!";
        return false;
    }

    //====== 显示尺寸：SAR（像素宽高比）+ 旋转元数据 ======
    //两类典型素材必须靠这两项才能正确显示：
    //1) 变形像素：720x480 的老 AVI/DVD，存储宽高比≠显示宽高比，直接拉伸会变形；
    //2) 手机竖拍：像素是横屏的，旋转 90° 元数据写在流里，不处理画面会躺倒。
    //这里一次性算出最终"显示宽 x 显示高"与逆时针 90° 旋转次数，渲染端照此 letterbox。
    //AVRational sar = videoStream->sample_aspect_ratio;//直接读字段，可能是 0/1（未知），没有任何兜底
    AVRational sar = av_guess_sample_aspect_ratio(p_formatCtx, videoStream, nullptr);//先取 stream->sample_aspect_ratio，如果无效（num 或 den 为 0），再退回去看 stream->codecpar->sample_aspect_ratio，最后还可能结合 format 上下文做推断；如果都拿不到，返回 0/1 表示"不知道"
    //sar = av_guess_sample_aspect_ratio(p_formatCtx, videoStream, frame);//再叠加 frame 级别的 SAR（由解码器从码流 VUI 里解析出来的）,由于我们这里解封装不像涉及到解码，就不使用这个了，减少耦合度
    if(sar.num <= 0 || sar.den <= 0)
        sar = (AVRational){1, 1}; //如果存在问题，直接设置1：1

    double dw = v_width * r2d(sar);
    double dh = v_height;
    //获取旋转次数
    v_rotate = (streamRotateCCWDegrees(videoStream)/90) % 4;
    //90或270度显示宽高互换
    if(v_rotate == 1 || v_rotate == 3){
        double t = dw;
        dw = dh;
        dh = t;
    }
    //赋值給全局变量
    v_turnWidth = dw;
    v_turnHeight = dh;
    //流级色彩范围兜底
    v_colorRange = int(videoStream->codecpar->color_range);
    v_is709 = (videoStream->codecpar->color_trc == AVCOL_TRC_BT709 ||
               videoStream->codecpar->color_space == AVCOL_SPC_BT709 ||
               videoStream->codecpar->color_trc == AVCOL_TRC_IEC61966_2_1);

    mux.unlock();
    //输出一下我们的视频信息
    qDebug()<<"=========video information===========";
    qDebug()<<"video  width: "<<v_width;
    qDebug()<<"video height: "<<v_height;
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

//读取视频信息，返回文件包，空间需要调用者释放：av_packet_free
AVPacket *TDemux::read()
{
    mux.lock();
    if(!p_formatCtx){
        mux.unlock();
        return NULL;
    }
    //为视频包变量初始化分配空间
    AVPacket *pack = av_packet_alloc();
    //从封装格式中读取一个压缩数据包的函数,结果存储在pack中
    int ret = av_read_frame(p_formatCtx, pack);
    //未正确获取信息
    if(ret != 0){
        //首先释放pack空间
        av_packet_free(&pack);
        //区分"文件读尽"与"网络抖动"，供解封装线程决定是否发 EOF 收尾
        m_eof = (ret == AVERROR_EOF);
        mux.unlock();
        return NULL;
    }
    m_eof = false;

    //pts/dts 统一转换为毫秒，便于后续音画同步；原写法用浮点乘法在大 time_base 下会溢出
    int idx = pack->stream_index;
    if (idx >= 0 && idx < (int)p_formatCtx->nb_streams) {
        AVRational tb = p_formatCtx->streams[idx]->time_base;
        pack->pts = toMs(pack->pts, tb);
        pack->dts = toMs(pack->dts, tb);
    }
    mux.unlock();
    return pack;
}


//获取视频参数，返回空间需要调用者释放 avcodec_parameters_free
AVCodecParameters *TDemux::copyVideoPara()
{
    mux.lock();
    //video_idx解封装的时候已经判断，这里再判断下是提示下
    if(!p_formatCtx || video_idx <0){
        mux.unlock();
        return NULL;
    }
    //分配视频解码参数空间
    AVCodecParameters *videoPara = avcodec_parameters_alloc();
    //进行拷贝
    int ret = avcodec_parameters_copy(videoPara, p_formatCtx->streams[video_idx]->codecpar);
    //拷贝失败返回NULL;
    if(ret < 0){
        mux.unlock();
        return NULL;
    }
    mux.unlock();
    return videoPara;

}


//获取音频参数，返回空间需要调用者释放 avcodec_parameters_free
AVCodecParameters *TDemux::copyAudioPara()
{
    mux.lock();
    //video_idx解封装的时候已经判断，这里再判断下是提示下
    if(!p_formatCtx || audio_idx <0){
        mux.unlock();
        return NULL;
    }
    //分配视频解码参数空间
    AVCodecParameters *audioPara = avcodec_parameters_alloc();
    //进行拷贝
    int ret = avcodec_parameters_copy(audioPara, p_formatCtx->streams[audio_idx]->codecpar);
    //拷贝失败返回NULL;
    if(ret < 0){
        mux.unlock();
        return NULL;
    }
    mux.unlock();
    return audioPara;

}


//seek位置 pos 0.0-1.0
bool TDemux::seek(double pos)
{
    if (pos < 0) pos = 0;
    if (pos > 1) pos = 1;
    mux.lock();
    //video_idx解封装的时候已经判断，这里再判断下是提示下
    if(!p_formatCtx || video_idx <0){
        mux.unlock();
        return false;
    }
    //清理读取缓冲
    avformat_flush(p_formatCtx);
    //视频流
    AVStream *videoStream = p_formatCtx->streams[video_idx];
    qint64 ts;
    if(videoStream->duration > 0 && videoStream->duration != AV_NOPTS_VALUE){
        ts = qint64(videoStream->duration*pos);  //获取跳转点（流 time_base 单位）
    }else{
        qint64 us = (p_formatCtx->duration > 0) ? qint64(p_formatCtx->duration * pos) : 0;
        ts = av_rescale_q(us, AV_TIME_BASE_Q, videoStream->time_base);
    }
    //AVSEEK_FLAG_FRAME 是按帧号定位，这里给的是时间戳，不能带该标志
    //改为 -1（全部流一起定位）：只按 video_idx seek 时音频会停在旧位置，seek 后音画必然错位
    //stream_index=-1 时时间戳须为 AV_TIME_BASE（微秒）单位
    qint64 tsUs = av_rescale_q(ts, videoStream->time_base, AV_TIME_BASE_Q);
    int ret = av_seek_frame(p_formatCtx, -1, tsUs, AVSEEK_FLAG_BACKWARD);
    mux.unlock();
    return ret >= 0;
}




void TDemux::clear()
{
    mux.lock();
    if (!p_formatCtx) {
        mux.unlock();
        return;
    }
    avformat_flush(p_formatCtx);
    mux.unlock();
}


//判断是否是音频
bool TDemux::isAudio(AVPacket *pack)
{
    if(!pack)
        return false;
    //准确判断是否是音频流的片源
    //带字幕流/数据流的片源，字幕包会被塞进音频队列送进音频解码器，
    //表现为音频解码报错、声音异常或直接无声。必须精确匹配音频流索引。
    mux.lock();
    bool ok = (audio_idx >= 0 && pack->stream_index == audio_idx);
    mux.unlock();
    return ok;
}
//判断释放是视频
bool TDemux::isVideo(AVPacket *pack)
{
    if(!pack)
        return false;
    mux.lock();
    bool ok = (video_idx >= 0 && pack->stream_index == video_idx);
    mux.unlock();
    return ok;
}
//根据解封装错误代码，获取具体报错内容
QString TDemux::getOpenErrText() const
{
    switch (v_openErr) {
    case OE_OPEN_FAILED:
        return QString("无法打开文件（不是受支持的媒体文件或路径无效）：%1").arg(v_openErrDetail);
    case OE_NO_STREAM_INFO:
        return QString("文件信息探测失败（可能损坏/截断或扩展名与实际格式不符）：%1").arg(v_openErrDetail);
    case OE_NO_VIDEO:
        return QString("没有视频流（当前版本不支持纯音频播放）");
    case OE_BAD_SIZE:
        return "视频尺寸信息无效，文件可能损坏";
    default:
        return "未知错误";
    }
}

