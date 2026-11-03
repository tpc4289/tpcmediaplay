#ifndef TMEDIAPLAY_H
#define TMEDIAPLAY_H

#include "tmediaplaylist.h"

#include <QWidget>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QGroupBox>


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

public:
    //播放模式（与 modelBtn 图标顺序一致：播完停止→单曲循环→列表循环→随机播放）
    enum PlayMode {
        MODE_ONCE = 0,     //播完停止
        MODE_ONE_CYCLE,    //单曲循环
        MODE_ALL_CYCLE,    //列表循环
        MODE_RANDOM,       //随机播放
        MODE_COUNT
    };

private:
    TDemuxThread *demuxThread  = nullptr;
    TDecode *videoDecode  = nullptr;
    TDecode *audioDecode  = nullptr;
    TVideoWidget *videoWidget  = nullptr;
    //视频解码显示
    TVideoThread *videoThread  = nullptr;
    //音频线程重采样播放
    TAudioThread *audioThread  = nullptr;

    //播放列表面板（主窗口右侧滑出/缩回）
    TMediaPlayList *playList = nullptr;


    //界面播放元素
    QGroupBox *mediaGroupBox = nullptr; //视频播放控制区域
    QSlider *seekSlider = nullptr; //进度条
    QLabel *durTimeLab = nullptr; //显示播放时间
    QPushButton *stopBtn = nullptr;//停止播放按钮
    QPushButton *backBtn = nullptr;//上一个按钮
    QPushButton *playBtn = nullptr;//播放按钮
    QPushButton *forwardBtn = nullptr;//下一个按钮
    QPushButton *modelBtn = nullptr; //播放模式按钮
    QPushButton *soundBtn = nullptr;//静音/打开按钮
    QSlider *volumnSlider = nullptr;//音量大小拖动条
    QPushButton *playListBtn = nullptr;//播放列表按钮
    QPushButton *openFileBtn = nullptr;//打开文件按钮
    QPushButton *fullScreenBtn = nullptr; //全屏按钮
    QPushButton *exitFullBtn = nullptr; //退出全屏按钮


    QTimer *timer = nullptr; //用来显示播放进度的时间


    //控件自动隐藏：打开播放器 2 秒后只剩视频画面；
    //鼠标在窗口内移动立即重现控制条，静止 2 秒后再次收起
    QTimer *autoHideTimer = nullptr;
    bool controlsVisible = true;
    void mainBoxGroupHideTimer();//播放控制条自动显示和隐藏函数
    void showControls();
    void hideControls();
    void restartAutoHide();
    void tryAutoHide();             //定时到点：有交互迹象则续期，否则收起



    //EOF 后先等音频尾音播完再推进播放模式，避免片尾被掐断
    QTimer *eofTimer = nullptr;   //轮询缓冲是否排空
    QElapsedTimer eofElapsed;     //EOF 至今耗时

    void advanceByMode();         //轮询满足后按模式执行下一步

    int modelIndex = MODE_ONCE; //记录播放模式图标索引

    bool playing = false;
    bool paused = false;

    //当前播放文件路径（换源/循环/随机都用得到）
    QString currentUrl;

    //用来记录音量大小
    qreal volumnSize = 1.0;
    //记录是否静音
    bool isMute = false;

    //关闭所有资源，停止线程
    void closeAll();

    //按播放模式在列表中前进/后退一个文件并播放
    void playIndex(int row);
    void playAdjacent(bool next);
    //切换进度条时间标签预览（拖动中实时显示）
    void showSeekPreview(int permille);


protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

    //全局事件过滤：播放列表滑出后，点击任意其他地方收起
    bool eventFilter(QObject *watched, QEvent *event) override;


public:
    explicit tmediaplay(QWidget *parent = nullptr);
    ~tmediaplay() override;
    //打开并播放（在 show 之后调用）。支持任意次换源：内部先彻底关闭上一路再打开新文件
    void play(const QString &url);

    //外部文件入口：加入播放列表并立即播放（命令行/二次实例转交）
    void openMediaFile(const QString &path);

    //视频进度拖动跳转
    void mediaSeek(int permille);


private:
    Ui::tmediaplay *ui;


private slots:
    void onUpdateClockValue();
    void onPlayBtnClicked(); //播放按钮点击槽函数
    void onStopBtnClicked(); //停止按钮点击槽函数


    void onModelBtnClicked(); //播放模式按钮点击槽函数
    void onEndOfReached();    //当前文件播完：按播放模式决定下一步

    void onSoundBtnClicked(); //静音按钮点击槽函数
    void onVolumnSliderValChanged(int size); //音量大小调整槽函数

    void onPlayListBtnClicked();//播放列表打开文件列表
    void onOpenFileBtnClicked();//打开文件按钮事件槽函数
    void onFullScreenBtnClicked(); //全屏按钮点击
    void onExitFullBtnClicked();//退出全屏按钮点击

};
#endif // TMEDIAPLAY_H
