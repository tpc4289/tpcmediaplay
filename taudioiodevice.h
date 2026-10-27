#ifndef TAUDIOIODEVICE_H
#define TAUDIOIODEVICE_H

#include <QIODevice>
#include <deque>
#include <mutex>

struct AVFrame;


#define CLOCK_NOT_READY (std::numeric_limits<int64_t>::min())

class TAudioIODevice : public QIODevice
{
    Q_OBJECT
public:
    explicit TAudioIODevice(QObject *parent = nullptr);
    ~TAudioIODevice();

    //把重采样后的音频frame写入队列（内部 av_frame_ref 持有副本，调用方随后即可释放原帧）。满返回 false
    bool pushQueue(AVFrame *frame);
    int64_t getClock();

    qint64 getBytesAvailable() const;

protected:
    //重载实现数据写类
    qint64 readData(char *data, qint64 maxlen) override;
    //虽然该函数不需要用到，但是因为是纯虚函数，我们还是实现它
    qint64 writeData(const char *data, qint64 len) override;
    //清空所有未播完的帧（seek 用），时钟回到未就绪
    //注意：不能命名为 reset()，会与 QIODevice::reset() 虚函数返回类型冲突
    void clearBuffer();
    bool isEmpty();
    qint64 bytesAvailable() const override;
    bool isSequential() const override;



private:
    //互斥锁
    mutable std::mutex mux;
    //本垒持有的帧引用
    std::deque<AVFrame *> frame_queue;
    //所取帧已经消费字节
    qint64 frame_pos = 0;

    int64_t clock = CLOCK_NOT_READY; //最近一次消费位置(毫秒)


    static const int MAX_FRAMES = 60;   //约 1.3s（48k/1024 帧长），满则倒逼解码线程

signals:
    void readyRead();


};

#endif // TAUDIOIODEVICE_H
