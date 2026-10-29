#ifndef TMEDIAPLAY_H
#define TMEDIAPLAY_H

#include <QWidget>
#include <QSlider>
#include <QPushButton>
#include <QLabel>


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


    //界面播放元素
    QSlider *seekSlider = nullptr; //进度条
    QLabel *durTimeLab = nullptr; //显示播放时间
    QPushButton *stopBtn = nullptr;//停止播放按钮
    QPushButton *backBtn = nullptr;//后退按钮
    QPushButton *playBtn = nullptr;//播放按钮
    QPushButton *forwardBtn = nullptr;//前进按钮
    QPushButton *soundBtn = nullptr;//静音/打开按钮
    QSlider *volumnSlider = nullptr;//音量大小拖动条


    bool playing = false;
    bool paused = false;

    //用来记录音量大小
    qreal volumnSize = 0.0;
    //记录是否静音
    bool isMute = false;



public:
    explicit tmediaplay(QWidget *parent = nullptr);
    ~tmediaplay() override;
    //打开并播放（在 show 之后调用）。重复调用不支持换源，仅播一次
    void play(const QString &url);

private:
    Ui::tmediaplay *ui;

private slots:
    void onPlayBtnClicked(); //播放按钮点击槽函数
    void onStopBtnClicked(); //停止按钮点击槽函数

    void onSoundBtnClicked(); //静音按钮点击槽函数
    void onVolumnSliderValChanged(int size); //音量大小调整槽函数

};
#endif // TMEDIAPLAY_H
