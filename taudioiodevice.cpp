#include "taudioiodevice.h"

#include <QDebug>

extern "C"{
#include <libavcodec/avcodec.h>
}

TAudioIODevice::TAudioIODevice(QObject *parent)
    : QIODevice{parent}
{
    open(QIODevice::ReadOnly);
}

TAudioIODevice::~TAudioIODevice()
{

}
//把重采样后的音频frame写入队列（内部 av_frame_ref 持有副本，调用方随后即可释放原帧）。满返回 false
bool TAudioIODevice::pushQueue(AVFrame *frame)
{
    //空frame，返回false
    if(!frame)
        return false;
    //加锁
    std::lock_guard<std::mutex> lock(mux);
    //队列满
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
    return true;
}

qint64 TAudioIODevice::readData(char *data, qint64 maxlen)
{
    if(maxlen <= 0)
        return 0;

    //锁
    std::lock_guard<std::mutex> lock(mux);
    if(frame_queue.empty()){
        //队列为空，给0
        memset(data, 0, size_t(maxlen));
        frame_pos = 0;
        return maxlen;
    }

    qint64 total = 0;
    while(total < maxlen){
        //获取队列中的首元素
        AVFrame *f = frame_queue.front();
        int ch = f->ch_layout.nb_channels;
        int frameBytes = f->nb_samples * ch * 2;

        if(frameBytes <= 0){
            av_frame_free(&frame_queue.front());
            //出列
            frame_queue.pop_front();
            frame_pos = 0;
            continue;
        }

        qint64 remain = frameBytes - frame_pos;
        qint64 need = maxlen - total;
        qint64 n = (need < remain) ? need : remain;

        memcpy(data+total, (char *)f->data[0] + frame_pos, size_t(n));
        frame_pos += n;
        total += n;

        //推进时钟：本帧pts(ms)+帧内已经消费比例
        double frameDurMs = 1000.0 * f->nb_samples / f->sample_rate;
        clock = f->pts + int64_t(frameDurMs * double(frame_pos)/double(frameBytes));

        if(frame_pos >= frameBytes){
            av_frame_free(&frame_queue.front());
            frame_queue.pop_front();
            frame_pos = 0;
        }
    }
    return total;
}

qint64 TAudioIODevice::writeData(const char *data, qint64 len)
{
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


    qint64 avail = 0;

    // 当前正在消费的那一帧剩余部分
    if (!frame_queue.empty()) {
        AVFrame *f = frame_queue.front();
        int ch = f->ch_layout.nb_channels;
        int frameBytes = f->nb_samples * ch * 2;
        if (frameBytes > frame_pos)
            avail += frameBytes - frame_pos;
    }

    // 队列里后续帧
    for (size_t i = 1; i < frame_queue.size(); ++i) {
        AVFrame *f = frame_queue[i];
        int ch = f->ch_layout.nb_channels;
        int frameBytes = f->nb_samples * ch * 2;
        if (frameBytes > 0)
            avail += frameBytes;
    }

    return avail + QIODevice::bytesAvailable();
}

//获取时钟总数
int64_t TAudioIODevice::getClock()
{
    std::lock_guard<std::mutex> lock(mux);
    return clock;
}
