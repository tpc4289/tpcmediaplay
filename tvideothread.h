#ifndef TVIDEOTHREAD_H
#define TVIDEOTHREAD_H

#include <QElapsedTimer>
#include <QThread>

#include "taudioiodevice.h"   //需要 CLOCK_NOT_READY 与音频时钟接口

class TDecode;
class TPacketQueue;
class TMediaPlayCtrl;
struct AVFrame;
struct SwsContext;

/*
 * 视频解码线程+音画同步+送显
 * 从视频队列取包解码 -> sws 统一转 YUV420P 打包 QByteArray
 *  按音频时钟 sync -> emit frameReady（Qt 自动拷贝，主线程渲染）
 *  AVFrame 不跨线程逃逸，无悬垂风险
 */
class TVideoThread : public QThread
{
    Q_OBJECT

private:
    //解码类指针
    TDecode *p_decode = nullptr;
    TPacketQueue *p_packetQueue = nullptr;
    SwsContext *swsCtx = nullptr;
    //控制类指针对象
    TMediaPlayCtrl *p_playCtrl = nullptr;
    int swsFmt = -1;
    int swsWidth = 0;
    int swsHeight = 0;
    double frameDelayMs = 40.0;   //无音频/时钟未就绪时的两帧间隔

    //音频主时钟来源（可为 nullptr）
    TAudioIODevice *p_audioDev = nullptr;
    //无音频兜底用的帧间计时
    QElapsedTimer paceTimer;

    //时钟停滞检测：音频缓冲被饿死时时钟不推进，若一直等下去画面会变成慢动作
    int64_t lastClock = CLOCK_NOT_READY;
    int clockStallCount = 0;

    //音画同步阈值（毫秒）。用 constexpr 避免类内 static const 在取地址时缺定义导致链接失败
    static constexpr int64_t SYNC_DROP_MS = -90;      //落后音频 90ms 以上：丢帧
    static constexpr int64_t SYNC_SLEEP_MS = 10;      //超前音频 10ms 以上：睡眠等待
    static constexpr int64_t SYNC_MAX_WAIT_MS = 200;  //单帧最多等 200ms，防时钟停摆卡死

    //解码帧 -> YUV420P，打包进 out（y + u + v 连续）
    bool frameToYuv(AVFrame *frame, int w, int h, QByteArray &out);
    //取空解码器当前所有帧，逐帧同步并送显
    void drainFrames();
    //音画同步：睡到音频时钟追上本帧 pts,返回 false 表示该帧严重落后应丢弃
    bool sync(int64_t pts);

    //无音频时钟时按 frameDelayMs 定节拍
    void paceByFrameRate();

    //显示几何与色彩参数（由主窗口在 play() 时从 TDemux 注入）：
    //SAR/旋转决定 letterbox 比例与 uv 旋转；色彩范围决定 shader 矩阵
    double turnWidth = 0;//考虑 SAR+旋转后的显示宽
    double turnHeight = 0;//考虑 SAR+旋转后的显示高
    int    videoRotate = 0; //逆时针 90° 旋转次数 0-3
    int    colorRange = 0; //流级色彩范围（AVColorRange），帧未标注时兜底
    bool   defBT709 = false;        //流级色彩空间是否 BT.709，帧未标注时兜底


public:
    explicit TVideoThread(QObject *parent = nullptr);
    ~TVideoThread();

    //组装：解码器 + 视频包队列 + 播放控制块 + 帧率（无音频时算帧间隔）
    void init(TDecode *decode, TPacketQueue *queue, double fps, TAudioIODevice *audioDev, TMediaPlayCtrl *playCtrl);
    //注入显示几何与色彩范围（每路文件打开后调用一次）
    void setDisplayInfo(double displayW, double displayH, int displayRot, int dColorRange, bool dIs709){
        turnWidth = displayW;
        turnHeight = displayH;
        videoRotate = displayRot;
        colorRange = dColorRange;
        defBT709 = dIs709;
    }


protected:
    void run() override;


signals:
    //跨线程送显：自动 QueuedConnection，参数按值拷贝转发到主线程
    //dispAspect = 显示宽/高（旋转+SAR 已折算）；vRotate 逆时针90°次数；
    //fullRange 色彩范围（true=0-255 JPEG，false=16-235 MPEG）
    void frameReady(QByteArray yuv, int width, int height, double dispAspect, int vRotate, bool fullRange, bool is709);
};

#endif // TVIDEOTHREAD_H
