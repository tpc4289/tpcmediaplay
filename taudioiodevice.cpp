#include "taudioiodevice.h"

#include <QDebug>

extern "C"{
#include <libavcodec/avcodec.h>
}

//帧的总字节数，脏帧返回0,S16的styleBytes传入值为2
static qint64 getFrameBytes(const AVFrame *f, qint16 styleBytes){
    if(!f || f->nb_samples <= 0 || f->ch_layout.nb_channels <= 0)
        return 0;
    return qint64(f->nb_samples) * f->ch_layout.nb_channels * styleBytes;   //2 = sizeof(int16_t)
}



TAudioIODevice::TAudioIODevice(QObject *parent)
    : QIODevice{parent}
{
    open(QIODevice::ReadOnly);
}

TAudioIODevice::~TAudioIODevice()
{
    clearBuffer();
}
//把重采样后的音频frame写入队列（内部 av_frame_ref 持有副本，调用方随后即可释放原帧）。满返回 false
bool TAudioIODevice::pushQueue(AVFrame *frame)
{
    //空frame，返回false
    if(!frame)
        return false;
    //加锁
    std::lock_guard<std::mutex> lock(mux);
    //队列满 满：调用方必须等待重试，不能丢帧
    if((int)frame_queue.size() >= MAX_FRAMES)
        return false;

    //分配frame空间
    AVFrame *clone = av_frame_alloc();
    if(!clone)
        return false;
    int ret = av_frame_ref(clone, frame);
    if(ret < 0){
        qDebug()<<"TAudioIODevice::pushQueue av_frame_ref failed!";
        av_frame_free(&clone);
        return false;
    }
    //写入队列末尾
    frame_queue.push_back(clone);
    //不要再在本类重新声明 readyRead 信号：原实现写了 signals: void readyRead()，
    //与基类 QIODevice::readyRead 同名遮蔽，emit 的是本类的，QAudioSink 连的是基类的，
    //就绪通知可能永远到不了 → 无声。删除该声明后这里 emit 的就是基类信号。
    emit readyRead();
    return true;
}

qint64 TAudioIODevice::readData(char *data, qint64 maxlen)
{

    qDebug() << "readData maxlen:" << maxlen;
    if(maxlen <= 0)
        return 0;

    //锁
    std::lock_guard<std::mutex> lock(mux);
    //该实现会导致sink收到0，状态变成idle，就一直停止了
    // if(frame_queue.empty()){
    //     //队列为空，给0
    //     memset(data, 0, size_t(maxlen));
    //     frame_pos = 0;
    //     return 0;
    // }

    qint64 total = 0;
    while(total < maxlen && !frame_queue.empty()){
        //获取队列中的首元素
        AVFrame *f = frame_queue.front();
        //int ch = f->ch_layout.nb_channels;
        qint64 frameBytes = getFrameBytes(f, 2);

        if(frameBytes <= 0){ //脏帧，丢掉再继续
            av_frame_free(&frame_queue.front());
            //出列
            frame_queue.pop_front();
            frame_pos = 0;
            continue;
        }

        qint64 remain = frameBytes - frame_pos; //本帧还剩多少字节
        qint64 need = maxlen - total;
        qint64 n = (need < remain) ? need : remain;
        n &= ~(qint64)1; //对齐到 s16 样本边界
        if(n<=0) break;

        memcpy(data+total, (char *)f->data[0] + frame_pos, size_t(n));
        frame_pos += n;
        total += n;

        //推进时钟：本帧pts(ms)+帧内已经消费比例
        //pts 无效（AV_NOPTS_VALUE/负值）时不能拿来算，否则时钟被推到哨兵附近，视频会乱跳
        if(f->pts != AV_NOPTS_VALUE && f->pts >=0 && f->sample_rate > 0){
            double frameDurMs = 1000.0 * f->nb_samples / f->sample_rate;
            clock = f->pts + int64_t(frameDurMs * double(frame_pos)/double(frameBytes));
        }

        if(frame_pos >= frameBytes){
            av_frame_free(&frame_queue.front());
            frame_queue.pop_front();
            frame_pos = 0;
        }
    }
    //关键修复：原实现队列空时 memset 后 return 0。pull 模式下返回 0 会让
    //QAudioSink 判定"无数据可取"而停摆（Idle/Stopped 后再也不来），表现即完全无声。
    //改为不足部分补静音并如实返回 maxlen：声卡持续被喂数据保持 Active；
    //此时真实时钟不推进，视频端由 sync() 的停滞检测兜底。
    if(total < maxlen){
        memset(data + total, 0, size_t(maxlen - total));
        total = maxlen;
    }
    return total;
}

qint64 TAudioIODevice::writeData(const char *data, qint64 len)
{
    Q_UNUSED(data);
    Q_UNUSED(len);
    return -1;
}

void TAudioIODevice::clearBuffer()
{
    std::lock_guard<std::mutex> lock(mux);
    while(!frame_queue.empty()){
        av_frame_free(&frame_queue.front());
        frame_queue.pop_front();
    }
    frame_pos = 0;
    clock = CLOCK_NOT_READY;

}

bool TAudioIODevice::isEmpty()
{
    std::lock_guard<std::mutex> lock(mux);
    return frame_queue.empty();
}

qint64 TAudioIODevice::bytesAvailable() const
{
    std::lock_guard<std::mutex> lock(mux);


    // qint64 avail = 0;

    // // 当前正在消费的那一帧剩余部分
    // if (!frame_queue.empty()) {
    //     AVFrame *f = frame_queue.front();
    //     int ch = f->ch_layout.nb_channels;
    //     int frameBytes = f->nb_samples * ch * 2;
    //     if (frameBytes > frame_pos)
    //         avail += frameBytes - frame_pos;
    // }

    // // 队列里后续帧
    // for (size_t i = 1; i < frame_queue.size(); ++i) {
    //     AVFrame *f = frame_queue[i];
    //     int ch = f->ch_layout.nb_channels;
    //     int frameBytes = f->nb_samples * ch * 2;
    //     if (frameBytes > 0)
    //         avail += frameBytes;
    // }

    // return avail + QIODevice::bytesAvailable();
    //这是"广告值"而不是真实剩余量：QAudioSink 用它决定来取多少，返回 0 就不再发起读取。
    //有数据时按此量被取走；没数据时 readData 补静音。真实待播字节数用 getBytesAvailable()。
    return 4096;
}

bool TAudioIODevice::isSequential() const
{
    return true;
}
//s16 交织帧的总字节数；脏帧返回 0
qint64 TAudioIODevice::queuedBytesLocked() const
{
    if(frame_queue.empty())
        return 0;

    qint64 avail = 0;
    //正在消费的那一帧剩余部分
    qint64 firstBytes = getFrameBytes(frame_queue.front(), 2);
    if(firstBytes > frame_pos)
        avail += firstBytes - frame_pos;
    //后续整帧
    for(size_t i = 1; i < frame_queue.size(); ++i){
        qint64 b = getFrameBytes(frame_queue[i], 2);
        if(b > 0)
            avail += b;
    }
    return avail;
}

//获取时钟总数
int64_t TAudioIODevice::getClock()
{
    std::lock_guard<std::mutex> lock(mux);
    return clock;
}

qint64 TAudioIODevice::getBytesAvailable() const
{
    std::lock_guard<std::mutex> lock(mux);
    return queuedBytesLocked();
}
