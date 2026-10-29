#ifndef TMEDIAPLAYCTRL_H
#define TMEDIAPLAYCTRL_H

#include <atomic>

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

};



#endif // TMEDIAPLAYCTRL_H
