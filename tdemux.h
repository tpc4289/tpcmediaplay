#ifndef TDEMUX_H
#define TDEMUX_H
#include <QMutex>

struct AVFormatContext;
struct AVPacket;
struct AVCodecParameters;

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
    //帧率（double：23.976 这类分数帧率截断成 int 会让无音频文件的定拍偏慢）
    double v_fps = 0;
    //视频总时长-毫秒
    int v_totalMs = 0;
    //文件结尾
    std::atomic<bool> m_eof{false};
    //最近一次 open() 失败原因
    int v_openErr = OE_OK;
    //FFMPEG获取到的错误信息
    QString v_openErrDetail;
    //考虑 SAR（像素宽高比）与旋转元数据后的"显示尺寸"：
    //老 AVI/DVD 变形像素、手机竖拍视频靠这两个值才能不拉伸、不躺倒
    double v_turnWidth = 0;
    double v_turnHeight = 0;
    //显示所需的逆时针 90° 旋转次数：0/1/2/3（源自 displaymatrix 元数据）
    int v_rotate = 0;
    //流级色彩范围标注（AVColorRange）：帧未携带时作为兜底
    int v_colorRange = 0;
    //流级色彩空间是否 BT.709（AVColorSpace）：手机/网络视频主流是 709，
    //帧未标注时作为渲染矩阵选择的兜底依据
    bool v_is709 = false;

public:

    //打开失败的具体类型：界面向用户提示"为什么播不了"
    enum MediaOpenErr{
        OE_OK = 0,
        OE_OPEN_FAILED,      //容器都打不开：路径不存在/不是媒体文件
        OE_NO_STREAM_INFO,   //流信息探测失败：文件损坏/截断
        OE_NO_VIDEO,         //没有视频流（纯音频文件当前版本不支持）
        OE_BAD_SIZE,         //视频尺寸无效
    };

    TDemux();
    ~TDemux();
    //获取媒体总时长(ms)，RTSP 等直播流为 0
    int getTotalMs() const {return v_totalMs;}
    //上一次 read() 是否读到文件尾（区分 EOF 与网络抖动）
    bool getIsEndOf() const {return m_eof;}
    //获取fps
    double getFps() const {return v_fps;}

    //是否存在音频流
    bool hasAudio() const;

    //打开媒体文件，或者流媒体 rtmp http rtsp
    virtual bool open(const char *url);
    //有打开就有关闭，主要是关闭解封装后的上下文对象
    virtual void close();
    //读取视频信息，返回文件包，空间需要调用者释放：av_packet_free
    virtual AVPacket *read();
    //获取视频参数，返回空间需要调用者释放 avcodec_parameters_free
    AVCodecParameters *copyVideoPara();
    //获取音频参数，无音频流时返回 NULL
    AVCodecParameters *copyAudioPara();
    //seek位置 pos 0.0-1.0
    virtual bool seek(double pos);
    //清理缓冲
    virtual void clear();
    //判断是否是音频（精确匹配音频流索引，不能用"非视频"代替）
    virtual bool isAudio(AVPacket *pack);
    //判断是否是视频
    virtual bool isVideo(AVPacket *pack);
    //根据解封装错误代码，获取具体报错内容
    virtual QString getOpenErrText() const;

    //获取转换后实际显示的视频宽和高接口方法
    virtual double getTurnWidth() const { return v_turnWidth; }
    virtual double getTurnHeight() const { return v_turnHeight; }
    //获取视频旋转次数
    virtual int getVideoRotate() const { return v_rotate; }
    //获取视频色彩范围（AVColorRange 枚举值，0=未标注）
    int getColorRange() const { return v_colorRange; }
    //流级色彩空间是否 BT.709（帧未标注时的兜底依据）
    bool getIsBT709() const { return v_is709; }


};

#endif // TDEMUX_H
