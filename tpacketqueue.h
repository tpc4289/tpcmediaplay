#ifndef TPACKETQUEUE_H
#define TPACKETQUEUE_H


#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <queue>
#include <atomic>



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
    int count = 0;//队列元素个数
    std::atomic<bool> isRunning{true};

    //指向播放控制块的 seek 标志（千分位，-1 表示无请求）。
    //作用：让阻塞在"队列满背压等待"里的解封装线程能被 seek 请求即时唤醒
    std::atomic<int> *seekReq = nullptr;



public:
    // static constexpr int64_t MAX_BYTES = 1 * 1024 * 1024;   //容量上限 3MB
    //分开设置视频和音频队列最大大小
    static constexpr int64_t VIDEO_MAX_BYTES = 1 * 1024 * 1024; //视频
    static constexpr int64_t AUDIO_MAX_BYTES = 1 * 32 * 1024; //音频

    TPacketQueue();
    ~TPacketQueue();
    //入队。调用方须持有 lk（= lock_guard/unique_lock over mux）
    //容量满时在本函数内 wait 等待消费者腾出空间
    virtual void push(TPacket *tp,int type, std::unique_lock<std::mutex> &lk);
    //出队（阻塞直到有数据或被停止）。返回 false 表示已停止
    virtual bool pop(TPacket *&tp);
    //清空队列
    virtual void flush();
    //停止：唤醒所有阻塞点，消费者将退出
    virtual void stop();
    //重启
    void restart();
    //获取队列元素个数
    virtual int getCount();

    //挂接 seek 标志指针（由 TDemuxThread 构造时设置）
    void setSeekRequest(std::atomic<int> *flag) { seekReq = flag; }
    //唤醒阻塞在"满等待"里的 push（UI 线程发起 seek 时调用）
    void wakePushWaiters() { condNotFull.notify_all(); }

    std::mutex mux;   //供外部（解封装线程）持锁调用 push

};

#endif // TPACKETQUEUE_H
