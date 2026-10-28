#ifndef TMEDIAPLAY_H
#define TMEDIAPLAY_H

#include <QWidget>


class TDemuxThread;
class TDecode;
class TVideoWidget;
class TVideoThread;
class TAudioThread;

QT_BEGIN_NAMESPACE
namespace Ui {
class tmediaplay;
}
QT_END_NAMESPACE

class tmediaplay : public QWidget
{
    Q_OBJECT

private:
    TDemuxThread *demuxThread  = nullptr;
    TDecode *videoDecode  = nullptr;
    TDecode *audioDecode  = nullptr;
    TVideoWidget *videoWidget  = nullptr;
    //视频解码显示
    TVideoThread *videoThread  = nullptr;
    //音频线程重采样播放
    TAudioThread *audioThread  = nullptr;

public:
    explicit tmediaplay(QWidget *parent = nullptr);
    ~tmediaplay() override;
    //打开并播放（在 show 之后调用）。重复调用不支持换源，仅播一次
    void play(const QString &url);

private:
    Ui::tmediaplay *ui;
};
#endif // TMEDIAPLAY_H
