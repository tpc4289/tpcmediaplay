#ifndef TMEDIAPLAY_H
#define TMEDIAPLAY_H

#include "tmediaplaylist.h"

#include <QWidget>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QCloseEvent>


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



    //播放列表框
    TMediaPlayList *playList = nullptr;


    //界面播放元素
    QSlider *seekSlider = nullptr; //进度条
    QLabel *durTimeLab = nullptr; //显示播放时间
    QPushButton *stopBtn = nullptr;//停止播放按钮
    QPushButton *backBtn = nullptr;//后退按钮
    QPushButton *playBtn = nullptr;//播放按钮
    QPushButton *forwardBtn = nullptr;//前进按钮
    QPushButton *modelBtn = nullptr; //播放模式按钮
    QPushButton *soundBtn = nullptr;//静音/打开按钮
    QSlider *volumnSlider = nullptr;//音量大小拖动条
    QPushButton *playListBtn = nullptr;//播放列表按钮
    QPushButton *openFileBtn = nullptr;//打开文件按钮
    QPushButton *fullScreenBtn = nullptr; //全屏按钮
    QPushButton *exitFullBtn = nullptr; //退出全屏按钮




    QTimer *timer = nullptr; //用来显示播放进度的时间


    int modelIndex = 0; //记录播放模式图标索引

    bool playing = false;
    bool paused = false;

    //用来记录音量大小
    qreal volumnSize = 0.0;
    //记录是否静音
    bool isMute = false;


    void closeAll();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;



public:
    explicit tmediaplay(QWidget *parent = nullptr);
    ~tmediaplay() override;
    //打开并播放（在 show 之后调用）。重复调用不支持换源，仅播一次
    void play(const QString &url);

    //视频进度拖动跳转
    void mediaSeek(int permille);

private:
    Ui::tmediaplay *ui;


private slots:
    void onUpdateClockValue();
    void onPlayBtnClicked(); //播放按钮点击槽函数
    void onStopBtnClicked(); //停止按钮点击槽函数


    void onModelBtnClicked(); //播放模式按钮点击槽函数

    void onSoundBtnClicked(); //静音按钮点击槽函数
    void onVolumnSliderValChanged(int size); //音量大小调整槽函数

    void onPlayListBtnClicked();//播放列表打开文件列表
    void onOpenFileBtnClicked();//打开文件按钮事件槽函数
    void onFullScreenBtnClicked(); //全屏按钮点击
    void onExitFullBtnClicked();//退出全屏按钮点击

};
#endif // TMEDIAPLAY_H
