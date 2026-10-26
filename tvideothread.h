#ifndef TVIDEOTHREAD_H
#define TVIDEOTHREAD_H

#include <QThread>

class TDecode;
class TPacketQueue;
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
    int swsFmt = -1;
    int swsWidth = 0;
    int swsHeight = 0;

    double frameDelayMs = 40.0;   //无音频时的两帧间隔




    //解码帧 -> YUV420P，打包进 out（y + u + v 连续）
    bool frameToYuv(AVFrame *frame, int w, int h, QByteArray &out);
    //取空解码器当前所有帧，逐帧同步并送显
    void drainFrames();
    //音画同步：睡到音频时钟追上本帧 pts
    bool sync(int64_t pts);

public:
    explicit TVideoThread(QObject *parent = nullptr);
    ~TVideoThread();

    //组装：解码器 + 视频包队列 + 播放控制块 + 帧率（无音频时算帧间隔）
    void init(TDecode *decode, TPacketQueue *queue, double fps);

protected:
    void run() override;


signals:
    //跨线程送显：自动 QueuedConnection，参数按值拷贝转发到主线程
    void frameReady(QByteArray yuv, int width, int height);
};

#endif // TVIDEOTHREAD_H
