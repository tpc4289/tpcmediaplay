#ifndef TPACKETQUEUE_H
#define TPACKETQUEUE_H


#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <queue>
#include <bits/unique_lock.h>



struct AVPacket;
//队列里的一项
struct TPacket {
    AVPacket *pkt = nullptr;   //解码数据；nullptr 表示 EOF（读到文件尾）
    int64_t pts = -1;          //展示时间，已在 TDemux::read 中统一换算为毫秒
};


class TPacketQueue
{
private:
    std::queue<TPacket *> packQueue;
    std::condition_variable condNotEmpty;
    std::condition_variable condNotFull;
    int64_t size = 0;
    std::atomic<bool> isRunning{true};

public:
    static constexpr int64_t MAX_BYTES = 3 * 1024 * 1024;   //容量上限 3MB
    TPacketQueue();
    ~TPacketQueue();
    //入队。调用方须持有 lk（= lock_guard/unique_lock over mux）
    //容量满时在本函数内 wait 等待消费者腾出空间
    virtual void push(TPacket *tp, std::unique_lock<std::mutex> &lk);
    //出队（阻塞直到有数据或被停止）。返回 false 表示已停止
    virtual bool pop(TPacket *&tp);
    //清空队列
    virtual void flush();
    //停止：唤醒所有阻塞点，消费者将退出
    virtual void stop();
    std::mutex mux;   //供外部（解封装线程）持锁调用 push

};

#endif // TPACKETQUEUE_H
