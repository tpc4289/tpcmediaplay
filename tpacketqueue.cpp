#include "tpacketqueue.h"
#include <qDebug>
extern "C"{
#include <libavcodec/avcodec.h>
}


/* 包接收队列
 * 解封装中读取文件包，即放到队列中
 * 因为文件读取包速度远远大于解码包获取帧显示过程，故用队列方式，而不采用信号槽机制
 */

TPacketQueue::TPacketQueue() {}

TPacketQueue::~TPacketQueue()
{}

//入队。调用方须持有 lk（= lock_guard/unique_lock over mux）
//容量满时在本函数内 wait 等待消费者腾出空间
void TPacketQueue::push(TPacket *tp, std::unique_lock<std::mutex> &lk)
{
    int64_t in_size = (tp->pkt ? tp->pkt->size:0);
    //判断是否已经满了，满了就阻塞
    condNotFull.wait(lk, [this, in_size]{
        return size + in_size <= MAX_BYTES || !isRunning;
    });
     // 被停止，丢弃
    if(!isRunning)
        return;
     // 2. 入队并累加 size
    packQueue.push(tp);
    count += 1;
    qDebug()<<"packet push, the size is:"<<tp->pkt->size;
    if(tp->pkt)
        size += in_size;
    // 3. 通知消费者有数据了
    condNotEmpty.notify_one();
}

//出队（阻塞直到有数据或被停止）。返回 false 表示已停止
bool TPacketQueue::pop(TPacket *&tp)
{
    std::unique_lock<std::mutex> lk(mux);
    condNotEmpty.wait(lk, [this] { return !packQueue.empty() || !isRunning; });
    if(packQueue.empty())
        return false;
    //获得队列中最先入队列的pack
    tp = packQueue.front();
    //取出该pack
    packQueue.pop();
    count -= 1;
    qDebug()<<"packet pop, the size is:"<<tp->pkt->size;
    if(tp->pkt)
        size -= tp->pkt->size;
    condNotFull.notify_all();
    return true;
}

//清空队列
void TPacketQueue::flush()
{
    std::lock_guard<std::mutex> lk(mux);
    while(!packQueue.empty()){
        TPacket *tp = packQueue.front();
        packQueue.pop();
        if(tp->pkt)
            av_packet_free(&tp->pkt);
        delete tp;
    }
    size = 0;
    count = 0;
    condNotFull.notify_all();
}

//停止：唤醒所有阻塞点，消费者将退出
void TPacketQueue::stop()
{
    isRunning = false;
    condNotEmpty.notify_all();
    condNotFull.notify_all();
}

int TPacketQueue::getCount()
{
    return count;
}
