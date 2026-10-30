#ifndef TMEDIAPLAYCTRL_H
#define TMEDIAPLAYCTRL_H

#include <atomic>
#include <limits>

//音频时钟未就绪的哨兵值：起播前、seek 清空缓冲后都是它
#define CLOCK_NOT_READY (std::numeric_limits<int64_t>::min())

//三条线程共享的播放控制块（由 TDemuxThread 持有，其余线程拿指针）
struct TMediaPlayCtrl{
    //置位后所有线程退出循环
    std::atomic_bool stopRequest{false};

    //暂停：解封装停读、音频挂起(时钟冻结)，视频 sync 等待 -> 画面自然停住
    std::atomic_bool paused{false};

    //seek 后置位，由对应消费线程读走并清理解码缓冲/播放缓冲
    std::atomic_bool flushVideo{false};
    std::atomic_bool flushAudio{false};

    //seek 请求（千分位 0-1000），-1 表示无请求；解封装线程读走执行
    std::atomic<int> seekPermille{-1};

    //音频已读到 EOF；视频线程据此切换为自时钟收尾，避免睡大觉
    std::atomic_bool audioEof{false};

    //音频主时钟(毫秒)。INT64_MIN 表示尚未就绪
    std::atomic<int64_t> audioClockMs{CLOCK_NOT_READY};

    //读走并复位 flush 标志
    bool takeFlush(std::atomic_bool TMediaPlayCtrl::*flag) {
        return (this->*flag).exchange(false);
    }
    //seek / 重新播放：清缓冲 + 时钟复位
    void requestFlushAll() {
        flushVideo = true;
        flushAudio = true;
        audioClockMs = CLOCK_NOT_READY;
        audioEof = false;
    }

};



#endif // TMEDIAPLAYCTRL_H
