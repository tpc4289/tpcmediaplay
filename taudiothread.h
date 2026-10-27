#ifndef TAUDIOTHREAD_H
#define TAUDIOTHREAD_H

#include <QThread>

class TDecode;
class TPacketQueue;
class QAudioFormat;
class QAudioDevice;
class TResample;
class QAudioSink;
class TAudioIODevice;

struct AVFrame;
class TAudioThread : public QThread
{
    Q_OBJECT
public:
    explicit TAudioThread(QObject *parent = nullptr);
    ~TAudioThread();

    virtual void init(TDecode *decode, TPacketQueue *queue);

    //单独送一帧
    virtual bool takeOne(AVFrame *frame);

    void run() override;
private:
    TDecode *p_decode = nullptr;
    TPacketQueue *p_packetQueue = nullptr;
    TResample *p_sample = nullptr;
    QAudioSink *p_sink = nullptr;
    TAudioIODevice *p_audioDev = nullptr;


    //选择设备支持的 s16 输出格式
    QAudioFormat chooseFormat(const QAudioDevice &dev);
};

#endif // TAUDIOTHREAD_H
