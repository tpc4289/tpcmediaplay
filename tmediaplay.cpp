#include "tmediaplay.h"
#include "ui_tmediaplay.h"
#include "tdemuxthread.h"
#include "tdecode.h"
#include "tvideowidget.h"
#include "tvideothread.h"
#include "taudiothread.h"

#include <QVBoxLayout>
#include <QSlider>
#include <QIcon>
#include <QGroupBox>
#include <QHBoxLayout>
#include <qaudiosink.h>
#include <QDir>
#include <QFileDialog>
#include <QRandomGenerator>
#include <QMessageBox>


//根据输入时间，计算分钟和秒，并格式化输出
static QString fmtTime(int64_t ms)
{
    if (ms < 0) ms = 0;
    int s = int(ms / 1000);
    return QString("%1:%2")
        .arg(s / 60, 2, 10, QChar('0'))
        .arg(s % 60, 2, 10, QChar('0'));
}

//四种播放模式的图标与提示（索引与 PlayMode 一致）
static const char *kModeIcons[4] = {
    ":/images/onestop.png",
    ":/images/onecycle.png",
    ":/images/allcycle.png",
    ":/images/random.png",
};


static const char *kModeTips[4] = {
    "播完停止（当前）",
    "单曲循环（当前）",
    "列表循环（当前）",
    "随机播放（当前）",
};


//尾音播完后按播放模式执行下一步
void tmediaplay::advanceByMode()
{
    if(!playing)
        return;
    //注意：EOF 到达时播放线程已自然收尾，这里先 closeAll 复位再开新源
    switch (modelIndex) {
    case MODE_ONE_CYCLE:
        //单曲循环：重播当前文件
        play(currentUrl);
        break;
    case MODE_ALL_CYCLE:
        //列表循环：播下一个，到底回第一个；列表为空则重播当前
        if(playList && playList->rowCount() > 0){
            int row = playList->indexOfPath(currentUrl);
            playIndex(row < 0 ? 0 : (row + 1) % playList->rowCount());
        } else {
            play(currentUrl);
        }
        break;
    case MODE_RANDOM:
        //随机播放：从列表随机取一个（多于一个时尽量不重复当前）
        if(playList && playList->rowCount() > 0){
            int cnt = playList->rowCount();
            int row = 0;
            if(cnt == 1){
                row = 0;
            } else {
                int cur = playList->indexOfPath(currentUrl);
                do {
                    row = QRandomGenerator::global()->bounded(cnt);
                } while(row == cur);
            }
            playIndex(row);
        } else {
            play(currentUrl);
        }
        break;
    case MODE_ONCE:
    default:
        //播完停止
        closeAll();
        seekSlider->setValue(0);
        durTimeLab->setText("00:00 / --:--");
        playBtn->setIcon(QIcon(":/images/play.png"));
        playBtn->setToolTip("播放");
        break;
    }
}

void tmediaplay::closeAll()
{
    if(timer)
        timer->stop();
    //换源/停止时终止待推进的 EOF 轮询，避免误触发模式推进再切一次歌
    if(eofTimer)
        eofTimer->stop();
    //1. 通知全部线程退出并停队列（唤醒阻塞在 pop/push 的读写线程）
    if(demuxThread)
        demuxThread->stopAll();

    //等消费者真正结束
    if(videoThread){
        videoThread->requestInterruption();
        videoThread->quit();
        videoThread->wait(3000);
    }
    if(audioThread){
        audioThread->requestInterruption();
        audioThread->quit();
        audioThread->wait(3000);
    }
    //3. 释放。顺序：线程对象 -> 解码器（解码器被线程引用，线程已停才安全）
    delete videoThread;
    videoThread = nullptr;
    delete audioThread;
    audioThread = nullptr;
    delete demuxThread;
    demuxThread = nullptr;
    delete videoDecode;
    videoDecode = nullptr;
    delete audioDecode;
    audioDecode = nullptr;

    playing = false;
    paused = false;
}

//播放列表中第 row 个文件（越界自动回绕）
void tmediaplay::playIndex(int row)
{
    if(!playList)
        return;
    const QStringList files = playList->playListFiles();
    if(files.isEmpty())
        return;
    if(row < 0)
        row = 0;
    if(row >= files.size())
        row = files.size() - 1;
    playList->setCurrentPlayingRow(row);
    play(files.at(row));
}

//上一个/下一个按钮：按当前播放模式取相邻文件
void tmediaplay::playAdjacent(bool next)
{
    if(!playList)
        return;
    const int cnt = playList->rowCount();
    if(cnt == 0)
        return;
    int cur = currentUrl.isEmpty() ? -1 : playList->indexOfPath(currentUrl);
    int target = -1;
    if(next){
        if(modelIndex == MODE_RANDOM && cnt > 1){
            do { target = QRandomGenerator::global()->bounded(cnt); } while(target == cur);
        } else {
            target = (cur < 0) ? 0 : (cur + 1) % cnt; //列表循环到尾部回头部
        }
    } else {
        target = (cur < 0) ? cnt - 1 : (cur - 1 + cnt) % cnt; //头部往前回尾部
    }
    playIndex(target);
}

//拖动进度条过程中：实时把"目标时间点"显示在时间标签上，先给用户反馈
void tmediaplay::showSeekPreview(int permille)
{
    if(!playing || !demuxThread)
        return;
    const int totalMs = demuxThread->totalMs();
    if(totalMs <= 0)
        return;
    const int64_t targetMs = int64_t(totalMs) * permille / 1000;
    durTimeLab->setText(fmtTime(targetMs) + " / " + fmtTime(totalMs));
}
void tmediaplay::closeEvent(QCloseEvent *event)
{
    //关窗前把线程全部收干净，避免退出时崩溃/卡死
    closeAll();
    event->accept();
}

//按键点击事件，实现按ESC键退出全屏
void tmediaplay::keyPressEvent(QKeyEvent *event)
{
    if(event->key() == Qt::Key_Escape && window()->isFullScreen()){
        window()->showNormal();
        fullScreenBtn->setVisible(true);
        exitFullBtn->setVisible(false);
        event->accept();
        return;
    }else if(event->key() == Qt::Key_Space){ //空格键点击事件，实现暂停播放
        onPlayBtnClicked(); //直接调用播放按钮事件
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

//窗口尺寸变化：播放列表面板重新贴右缘停靠
void tmediaplay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if(playList)
        playList->reposition();
}

//全局事件过滤：
//1) 播放列表滑出后，点击任意其他地方收起
//2) 控制条隐藏时，鼠标在窗口内移动 → 立即重新显示控制条并续倒计时
bool tmediaplay::eventFilter(QObject *watched, QEvent *event)
{
    if(event->type() == QEvent::MouseMove){
        QWidget *w = qobject_cast<QWidget *>(watched);
        if(w && w->window() == this){
            if(!controlsVisible)
                showControls();
            else
                restartAutoHide(); //任何移动都重置 2 秒计时
        }
    }
    if(event->type() == QEvent::MouseButtonPress && playList && playList->isSlideOut()){
        QWidget *w = qobject_cast<QWidget *>(watched);
        //列表内部（含其子控件）的点击不收起；列表按钮自身交给 clicked/toggle 处理
        if(w && w != playListBtn && !playList->isAncestorOf(w)){
            playList->slideIn();
        }
    }
    return QWidget::eventFilter(watched, event);
}

tmediaplay::tmediaplay(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::tmediaplay)
{
    ui->setupUi(this);
    //
    setWindowTitle("tpcmediaplay播放器");
    setWindowIcon(QIcon(":/images/window.png"));

    resize(900,640);

    //集成openGL显示的widget类
    videoWidget = new TVideoWidget(this);
    QGridLayout *gridLayout = new QGridLayout(this);
    gridLayout->setContentsMargins(0,0,0,0);
    //视频区放在第0行，设置跨3行，并铺满3行全部区域
    gridLayout->addWidget(videoWidget, 0, 0, 3, 1);
    gridLayout->setSpacing(0);

    const QString sliderStyle = R"(
        QSlider{
                background-color: rgba(0,0,0,0);
                height: 36px;
                padding: 0px 15px;
            }
        QSlider::groove:horizontal{
                height: 4px;
                background: #555555;
                border-radius: 2px;
            }
        QSlider::handle:horizontal{
                width:20px;
                height:12px;
                margin:-4px 0;
                background:#ffffff;
                border-radius:6px;}
        QSlider::sub-page:horizontal{
            background:#2196F3;
            border-radius:2px;})";

    //进度条选项
    seekSlider = new QSlider(Qt::Horizontal, this);
    seekSlider->setRange(0,1000);
    seekSlider->setStyleSheet(sliderStyle);

    //播放按钮控制区域对象
    mediaGroupBox = new QGroupBox(this);
    mediaGroupBox->setStyleSheet("QGroupBox{background-color: rgba(0,0,0,100);border: none;}");

    QHBoxLayout *hBoxLayout1 = new QHBoxLayout();
    hBoxLayout1->setContentsMargins(0,0,0,0);


    //播放进度
    durTimeLab = new QLabel("00:00 / --:--",this);
    durTimeLab->setStyleSheet("QLabel{color:#e8e8e8;font-size:13px;}");

    //按钮样式
    QString mediaBtnStyle = "QPushButton { border: none; background-color: transparent;border-radius: 6px;}"
                            "QPushButton:hover { background-color: rgba(255,255,255,40); }"
                            "QPushButton:pressed { background-color: rgba(255,255,255,70); }";
    //QString mediaBtnStyle = "QPushButton{width: 30px, height: 30px }";
    QSize btnIconSize(30,30);
    QSize btnSize(30,30);


    //停止播放按钮
    stopBtn = new QPushButton(this);
    stopBtn->setToolTip("停止播放");
    stopBtn->setIcon(QIcon(":/images/stop.png"));
    stopBtn->setIconSize(btnIconSize);
    stopBtn->setFixedSize(btnSize);
    stopBtn->setStyleSheet(mediaBtnStyle);

    //上一个按钮
    backBtn = new QPushButton(this);
    backBtn->setToolTip("上一个");
    backBtn->setIcon(QIcon(":/images/back.png"));
    backBtn->setIconSize(btnIconSize);
    backBtn->setFixedSize(btnSize);
    backBtn->setStyleSheet(mediaBtnStyle);

    //播放按钮
    playBtn = new QPushButton(this);    
    playBtn->setIcon(QIcon(":/images/play.png"));
    playBtn->setToolTip("播放");
    playBtn->setFixedSize(50,50);
    playBtn->setIconSize(QSize(50,50));
    playBtn->setStyleSheet(mediaBtnStyle);

    //下一个按钮
    forwardBtn = new QPushButton(this);
    forwardBtn->setToolTip("下一个");
    forwardBtn->setIcon(QIcon(":/images/forward.png"));
    forwardBtn->setFixedSize(btnSize);
    forwardBtn->setIconSize(btnIconSize);
    forwardBtn->setStyleSheet(mediaBtnStyle);


    //播放模式按钮
    // = {":/images/fullcycle.png",":/images/onecycle.png",":/images/random.png",":/images/onestop.png"};
    modelBtn = new QPushButton(this);
    modelBtn->setToolTip(kModeTips[MODE_ONCE]);
    modelBtn->setIcon(QIcon(kModeIcons[MODE_ONCE]));
    modelBtn->setFixedSize(btnSize);
    modelBtn->setIconSize(btnIconSize);
    modelBtn->setStyleSheet(mediaBtnStyle);

    //静音按钮
    soundBtn = new QPushButton(this);
    soundBtn->setIcon(QIcon(":/images/soundon.png"));
    soundBtn->setToolTip("静音");
    soundBtn->setFixedSize(btnSize);
    soundBtn->setIconSize(btnIconSize);
    soundBtn->setStyleSheet(mediaBtnStyle);

    //音量拖动条
    volumnSlider = new QSlider(Qt::Horizontal,this);
    volumnSlider->setRange(0,100);//设置值范围
    volumnSlider->setValue(100);//设置初始值
    volumnSlider->setFixedWidth(90);
    volumnSlider->setStyleSheet(sliderStyle);

    //播放列表按钮
    playListBtn = new QPushButton(this);
    playListBtn->setToolTip("播放列表");
    playListBtn->setIcon(QIcon(":/images/playlist.png"));
    playListBtn->setFixedSize(QSize(25,25));
    playListBtn->setIconSize(QSize(25,25));
    playListBtn->setStyleSheet(mediaBtnStyle);

    //打开文件按钮
    openFileBtn = new QPushButton(this);
    openFileBtn->setToolTip("打开文件");
    openFileBtn->setIcon(QIcon(":/images/openfile.png"));
    openFileBtn->setFixedSize(QSize(25,25));
    openFileBtn->setIconSize(QSize(25,25));
    openFileBtn->setStyleSheet(mediaBtnStyle);

    //全屏按钮
    fullScreenBtn = new QPushButton(this);
    fullScreenBtn->setToolTip("全屏");
    fullScreenBtn->setIcon(QIcon(":/images/fullscreen.png"));
    fullScreenBtn->setFixedSize(QSize(25,25));
    fullScreenBtn->setIconSize(QSize(25,25));
    fullScreenBtn->setStyleSheet(mediaBtnStyle);

    //退出全屏按钮
    exitFullBtn = new QPushButton(this);
    exitFullBtn->setToolTip("退出全屏");
    exitFullBtn->setIcon(QIcon(":/images/exitfull.png"));
    exitFullBtn->setFixedSize(btnSize);
    exitFullBtn->setIconSize(btnIconSize);
    exitFullBtn->setStyleSheet(mediaBtnStyle);
    exitFullBtn->setVisible(false);


    //媒体控制按钮一律不接受键盘焦点（Qt::NoFocus）。
    //Qt 默认按钮持有焦点时按空格=点击该按钮：点过"打开文件"后焦点留在按钮上，
    //再按空格就会再次弹出文件对话框；回车还会触发默认按钮，同理误触
    for(QPushButton *b : {stopBtn, backBtn, playBtn, forwardBtn, modelBtn,
                           soundBtn, playListBtn, openFileBtn, fullScreenBtn, exitFullBtn}){
        b->setFocusPolicy(Qt::NoFocus);
    }
    //进度条/音量条同样取消键盘焦点：鼠标拖动与滚轮不受影响，
    //避免点击后焦点停留在滑杆上改变空格键的走向
    seekSlider->setFocusPolicy(Qt::NoFocus);
    volumnSlider->setFocusPolicy(Qt::NoFocus);



    hBoxLayout1->addStretch();          // 【新增】左侧弹簧：把中间的图标向右推
    hBoxLayout1->addWidget(durTimeLab);
    //
    hBoxLayout1->addStretch();
    hBoxLayout1->addWidget(stopBtn);
    hBoxLayout1->addWidget(backBtn);

    hBoxLayout1->addWidget(playBtn);

    hBoxLayout1->addWidget(forwardBtn);
    hBoxLayout1->addWidget(modelBtn);
    hBoxLayout1->addStretch();
    hBoxLayout1->addWidget(soundBtn);
    hBoxLayout1->addWidget(volumnSlider);
    hBoxLayout1->addWidget(playListBtn);
    hBoxLayout1->addWidget(openFileBtn);
    hBoxLayout1->addWidget(fullScreenBtn);
    hBoxLayout1->addWidget(exitFullBtn);


    mediaGroupBox->setLayout(hBoxLayout1);
    mediaGroupBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);



    // 第一行拉伸，GL控件自动放大填满窗口
    gridLayout->setRowStretch(0, 1);

    gridLayout->addWidget(seekSlider, 1, 0,Qt::AlignBottom);

    gridLayout->addWidget(mediaGroupBox, 2, 0,Qt::AlignBottom);

    setLayout(gridLayout);

    timer = new QTimer(this);

    //EOF 轮询：等音频尾音播完再推进播放模式（上限 5s 防时钟异常卡死）
    eofTimer = new QTimer(this);
    eofTimer->setInterval(100);
    connect(eofTimer, &QTimer::timeout, this, [this]{
        bool drained = true;
        if(audioThread && !audioThread->isDisabled() && audioThread->audioDevice())
            drained = audioThread->audioDevice()->isEmpty();
        if(drained || eofElapsed.elapsed() > 5000){
            eofTimer->stop();
            advanceByMode();
        }
    });

    //播放列表窗口,主窗口右侧滑出/缩回（默认收起）
    playList = new TMediaPlayList(this);
    playList->hide();


    connect(seekSlider,&QSlider::sliderReleased,this,[this]{mediaSeek(seekSlider->value());});

    connect(playBtn,&QPushButton::clicked,this, &tmediaplay::onPlayBtnClicked);
    connect(stopBtn,&QPushButton::clicked,this, &tmediaplay::onStopBtnClicked);

    //前进/后退按钮：按播放模式在列表内切换上/下一个节目（原实现无任何功能）
    connect(backBtn,&QPushButton::clicked,this,[this]{ playAdjacent(false); });
    connect(forwardBtn,&QPushButton::clicked,this,[this]{ playAdjacent(true); });


    connect(modelBtn,&QPushButton::clicked,this, &tmediaplay::onModelBtnClicked);


    connect(soundBtn,&QPushButton::clicked,this, &tmediaplay::onSoundBtnClicked);

    connect(volumnSlider, &QSlider::valueChanged, this, &tmediaplay::onVolumnSliderValChanged);

    connect(playListBtn, &QPushButton::clicked, this, &tmediaplay::onPlayListBtnClicked);

    connect(openFileBtn, &QPushButton::clicked, this, &tmediaplay::onOpenFileBtnClicked);

    //全屏按钮点击事件驱动
    connect(fullScreenBtn, &QPushButton::clicked, this, &tmediaplay::onFullScreenBtnClicked);
    //退出全屏按钮点击事件驱动
    connect(exitFullBtn, &QPushButton::clicked, this, &tmediaplay::onExitFullBtnClicked);

    connect(timer, &QTimer::timeout, this, &tmediaplay::onUpdateClockValue);

    //播放文件双击，进行事情驱动响应
    connect(playList, &TMediaPlayList::playFileRequeted, this, &tmediaplay::play);

    //点击任意地方收起列表：监听全局鼠标事件，在 eventFilter 里判定
    qApp->installEventFilter(this);

    mainBoxGroupHideTimer(); //计时器，实现控件自动隐藏和显示

}

//计时器，实现控件自动隐藏和显示
void tmediaplay::mainBoxGroupHideTimer(){
    //控件自动隐藏：打开播放器 2 秒后只剩视频画面，鼠标移动立即重现
    autoHideTimer = new QTimer(this);
    autoHideTimer->setSingleShot(true);
    connect(autoHideTimer, &QTimer::timeout, this, &tmediaplay::tryAutoHide);
    autoHideTimer->start(2000);
}
//播放器控制区域显示控制
void tmediaplay::showControls()
{
    if(controlsVisible){
        restartAutoHide();
        return;
    }
    controlsVisible = true;
    if(mediaGroupBox)
        mediaGroupBox->setVisible(true); //控制区域可见
    if(seekSlider)
        seekSlider->setVisible(true); //进度条可见
    restartAutoHide(); //重新计时判断是否隐藏
}
//播放器控制区域隐藏控制
void tmediaplay::hideControls()
{
    if(!controlsVisible)
        return;
    controlsVisible = false;
    if(mediaGroupBox)
        mediaGroupBox->setVisible(false); //控制区域隐藏
    if(seekSlider)
        seekSlider->setVisible(false); //进度条隐藏
    //列表还开着就先收起，保证"只剩视频画面"
    if(playList && playList->isSlideOut())
        playList->slideIn();
}
//重新计时，2秒后触发tryAutoHide
void tmediaplay::restartAutoHide()
{
    if(autoHideTimer)
        autoHideTimer->start(2000);
}
//倒计时到点：仍在操作迹象（拖滑杆/列表展开/指针悬停控制条）则续期，否则收起
void tmediaplay::tryAutoHide()
{
    if(!controlsVisible)
        return;
    if((seekSlider && seekSlider->isSliderDown()) || (volumnSlider && volumnSlider->isSliderDown())){
        restartAutoHide();
        return;
    }
    if(playList && playList->isSlideOut()){
        restartAutoHide();
        return;
    }
    //鼠标停在按钮条/进度条上：说明用户正要操作，保持可见
    const QPoint p = mapFromGlobal(QCursor::pos());
    if(rect().contains(p)){
        for(QWidget *w = childAt(p); w; w = w->parentWidget()){
            if(w == mediaGroupBox || w == seekSlider){
                restartAutoHide();
                return;
            }
        }
    }
    hideControls();

}

tmediaplay::~tmediaplay()
{
    //关闭顺序很重要：先停"生产者"（解封装），再停"消费者"（音视频线程），
    //最后才释放它们引用的对象（解码器/队列/设备）。
    //只 delete ui，关窗时线程还在跑并访问即将析构的对象 → 崩溃/退出卡死
    // if(demuxThread){
    //     demuxThread->requestInterruption();
    //     //停队列唤醒可能卡在 push/pop 上的线程
    //     demuxThread->getVideoPackQueue()->stop();
    //     demuxThread->getAudioPackQueue()->stop();
    //     demuxThread->wait();
    // }
    // //线程对象析构内部会 requestInterruption + wait，这里显式先停更安全
    // delete videoThread;   videoThread = nullptr;
    // delete audioThread;   audioThread = nullptr;
    // delete demuxThread;   demuxThread = nullptr;

    // delete videoDecode;   videoDecode = nullptr;
    // delete audioDecode;   audioDecode = nullptr;
    closeAll();

    delete ui;
    ui = nullptr;
}

void tmediaplay::play(const QString &url)
{
    //换源关键修复：如果已有播放，先彻底关闭上一路（停线程+释放对象），
    //再开新文件。如果直接覆盖指针：旧线程继续跑并泄漏，双线程抢队列，
    //表现为"双击第二个文件不切换/卡死"
    if(playing || demuxThread)
        closeAll();

    currentUrl = url;
    //解封装线程
    demuxThread = new TDemuxThread();
    //打开失败（路径错/无视频流）则不启动任何线程，界面保持黑底并给出提示
    if (!demuxThread->openMedia(url.toUtf8().constData())) {
        qDebug() << "open failed:" << url;
        const QString errText = demuxThread->openErrText();
        delete demuxThread;
        demuxThread = nullptr;
        currentUrl.clear();
        QMessageBox::warning(this, "文件打开错误", errText, QMessageBox::Ok, QMessageBox::NoButton);
        return;
    }

    //新文件从头播：复位停止/暂停/seek 状态（closeAll 已删旧对象，这里保险起见再清一次）
    demuxThread->getMediaPlayCtrl()->stopRequest = false;
    demuxThread->getMediaPlayCtrl()->paused = false;

    TDemux *demux = demuxThread->getTDemux();
    TMediaPlayCtrl *playCtrl = demuxThread->getMediaPlayCtrl();


    //视频解码器
    videoDecode = new TDecode();

    if(!videoDecode->open(demux->copyVideoPara())){
        qDebug()<<"video decoder open failed!";
        //open 内部已释放 para，这里只需清理已创建的对象，避免泄漏
        delete videoDecode; videoDecode = nullptr;
        delete demuxThread; demuxThread = nullptr;
        return;
    }

    //videoDecode->send();

    //音频解码器：无音频流时 audioDecode 保持 nullptr，
    //音频线程会据此进入"只排空队列"模式（原实现此时传入未初始化野指针）
    AVCodecParameters *audioCodecPara = demux->copyAudioPara();
    //音频流可能为空，需要判断
    if(audioCodecPara){
        audioDecode = new TDecode();
        audioDecode->setIsAudio(true);
        //打开
        if(!audioDecode->open(audioCodecPara)){

            qDebug()<<"audio decoder open failed!";
            //open 无论成败都已释放 para；这里把对象置空，让音频线程走禁用分支
            delete audioDecode; audioDecode = nullptr;
        }
    }


    //音频线程：先 init。init 里会创建并启动音频设备（含主时钟），
    //这样视频线程随后就能拿到设备指针做同步
    audioThread = new TAudioThread();
    audioThread->init(audioDecode, demuxThread->getAudioPackQueue(), playCtrl);
    //qDebug() << "audio packet count is "<<demuxThread->getAudioPackQueue()->getCount();

    //视频解码显示
    videoThread = new TVideoThread();
    //关键接线：把音频设备（主时钟）交给视频线程，sync() 才有依据
    videoThread->init(videoDecode, demuxThread->getVideoPackQueue(), demux->getFps(), audioThread->audioDevice(), playCtrl);
    //qDebug() << "video packet count is "<<demuxThread->getVideoPackQueue()->getCount();
    //跨线程送显：TVideoThread 运行在工作线程、TVideoWidget 在主线程，
    //必须显式队列连接（参数按值拷贝）。不写会退化为 DirectConnection 风险写法，
    //在解码线程里直接调 update() 属于跨线程操作 GUI，未定义行为
    connect(videoThread, &TVideoThread::frameReady, videoWidget, &TVideoWidget::slotRepaint, Qt::QueuedConnection);


    //文件播完：按播放模式决定停止/循环/下一个/随机
    //emit 端在工作线程，必须队列连接回主线程
    connect(demuxThread, &TDemuxThread::endOfReached, this, &tmediaplay::onEndOfReached, Qt::QueuedConnection);

    //启动顺序：先消费者（音/视频线程），再生产者（解封装），
    //避免解封装抢跑把 3MB 队列塞满后卡在背压上
    audioThread->start();
    videoThread->start();
    demuxThread->start();


    //标注视频正在播放
    playing = true;
    playBtn->setIcon(QIcon(":/images/pause.png"));
    playBtn->setToolTip("暂停");
    paused = false;

    timer->start(500);
    //未静音
    isMute = false;

    //记录下音频音量大小
    // volumnSize = audioThread->audioSink()->volume();
    //每路节目起播都按界面音量条重新下发音量，并把上次 seek/暂停遗留的时钟复位
    if(QAudioSink *sink = audioThread->audioSink()){
        sink->setVolume(volumnSlider->value()/100.0);
        volumnSize = sink->volume();
    }
    //列表按钮高亮同步：标记当前播放行（双击列表时列表已自行标记过，这里兜底）
    if(playList){
        int row = playList->indexOfPath(url);
        if(row >= 0)
            playList->setCurrentPlayingRow(row);
    }

    //换源后时间标签先显示总时长占位
    durTimeLab->setText(QString("--:-- / %1").arg(fmtTime(demuxThread->totalMs())));
}


//外部文件入口（双击关联文件/命令行传入/二次实例转交）：
//入列表并立即换源播放——playing 时 play() 内部会先关旧再开新
void tmediaplay::openMediaFile(const QString &path)
{
    if(path.isEmpty())
        return;
    playList->addFiles(QStringList{path});
    play(path);
    //正在滑出的列表面板刷新到新当前项
    int row = playList->indexOfPath(path);
    if(row >= 0)
        playList->setCurrentPlayingRow(row);
}



//当拖动进度条时候，进行视频播放跳转
void tmediaplay::mediaSeek(int permille)
{
    if(!playing)
        return;
    if(paused){
        paused = false;
        playBtn->setIcon(QIcon(":/images/pause.png"));
        playBtn->setToolTip("暂停");
        demuxThread->setPause(false);
    }
    demuxThread->seekTo(permille/1000.0);
}

void tmediaplay::onUpdateClockValue()
{
    if(!playing || !demuxThread)
        return;
    //qDebug()<<"show clock start!";
    //原子性获取音频主时钟
    int64_t clock = demuxThread->getMediaPlayCtrl()->audioClockMs.load();
    int totalMs = demuxThread->totalMs();
    if (clock == CLOCK_NOT_READY) {
        //durTimeLab->setText(QString("--:-- / %1").arg(fmtTime(totalMs)));
        //seek/起播过渡期：拖拽预览优先，不能被时钟占位覆盖
        if(!seekSlider->isSliderDown())
            durTimeLab->setText(QString("--:-- / %1").arg(fmtTime(totalMs)));
        return;
    }
    if(!seekSlider->isSliderDown())
        durTimeLab->setText(fmtTime(clock) + " / " + fmtTime(totalMs));
    if (totalMs > 0 && !seekSlider->isSliderDown())
        seekSlider->setValue(int(clock * 1000 / totalMs));

}

//播放按钮点击响应事件
void tmediaplay::onPlayBtnClicked()
{
    if(!playing)
        return;
    //seek 时若处于暂停，自动恢复播放，避免"跳过去后画面不动"的困惑
    if(paused){
        paused = false;
        playBtn->setIcon(QIcon(":/images/pause.png"));
        playBtn->setToolTip("暂停");
    }else{
        paused = true;
        playBtn->setIcon(QIcon(":/images/play.png"));
        playBtn->setToolTip("播放");
    }
    demuxThread->setPause(paused);
   // qDebug()<<"play button clicked";
}
//停止按钮点击响应事件
void tmediaplay::onStopBtnClicked()
{
    closeAll();
    seekSlider->setValue(0);
    durTimeLab->setText("00:00 / --:--");
    playBtn->setIcon(QIcon(":/images/play.png"));
    playBtn->setToolTip("播放");
}

//播放模式按钮点击事件
void tmediaplay::onModelBtnClicked()
{
    // switch (modelIndex) {
    // case 0:
    //     modelBtn->setIcon(QIcon(":/images/onecycle.png"));
    //     modelIndex++;
    //     break;
    // case 1:
    //     modelBtn->setIcon(QIcon(":/images/allcycle.png"));
    //     modelIndex++;
    //     break;
    // case 2:
    //     modelBtn->setIcon(QIcon(":/images/random.png"));
    //     modelIndex++;
    //     break;
    // case 3:
    //     modelBtn->setIcon(QIcon(":/images/onestop.png"));
    //     modelIndex = 0;
    //     break;
    // default:
    //     break;
    // }
    modelIndex = (modelIndex + 1) % MODE_COUNT;
    modelBtn->setIcon(QIcon(kModeIcons[modelIndex]));
    modelBtn->setToolTip(kModeTips[modelIndex]);
}

//当前文件播放结束：先等音频缓冲播完（片尾不截断），再按播放模式决定下一步
void tmediaplay::onEndOfReached()
{
    if(!playing)
        return;
    //EOF 时视频线程已退出收尾，主 timer 继续跑也无意义，停掉防止读已结束线程
    if(timer)
        timer->stop();
    //启动尾音排空轮询；若音频禁用/缓冲已空，100ms 内即推进
    eofElapsed.restart();
    eofTimer->start();
}
void tmediaplay::onSoundBtnClicked()
{
    QAudioSink *curSink = (audioThread && audioThread->audioSink()) ? audioThread->audioSink() : nullptr;
    if(!curSink)
        return; //无音频流时不处理，避免空指针崩溃（原实现直接解引用）

    if(isMute){
        soundBtn->setIcon(QIcon(":/images/soundon.png"));
        soundBtn->setToolTip("静音");
        curSink->setVolume(volumnSize);
        //volumnSlider->setValue(volumnSize*100);
    }else{
        //记录静音前的音量值
        volumnSize = curSink->volume();
        soundBtn->setIcon(QIcon(":/images/soundoff.png"));
        soundBtn->setToolTip("声音");
        audioThread->audioSink()->setVolume(0.0);
        //volumnSlider->setValue(0);
    }
    isMute = !isMute;
}

void tmediaplay::onVolumnSliderValChanged(int size)
{
    QAudioSink *curSink = (audioThread && audioThread->audioSink()) ? audioThread->audioSink() : nullptr;
    qreal volSize = size/100.0;
    volumnSize = volSize;
    if(curSink)
        curSink->setVolume(volSize);
    //音量滑到 0 联动静音图标
    if(soundBtn)
        soundBtn->setIcon(QIcon(volSize <= 0.001 ? ":/images/soundoff.png" : ":/images/soundon.png"));
}

void tmediaplay::onPlayListBtnClicked()
{
    //单击：滑出；再单击按钮同样收起（点其他地方也会收起，由 eventFilter 处理）
    playList->slideToggle();
}

void tmediaplay::onOpenFileBtnClicked()
{
    QString path = QDir::rootPath();
    qDebug()<<"the path is "<< path;
    QString dlgTitle = "打开一个媒体文件";
    QString filter = "媒体文件(*.mp4 *.avi *.mkv *.mov *.flv *.wmv *.mpg *.mpeg *.ts *.m2ts "
                     "*.webm *.m4v *.3gp *.rmvb *.vob *.asf *.ogv);;所有文件(*.*)";
    QString fileName = QFileDialog::getOpenFileName(this, dlgTitle, path, filter);
    if(fileName.isEmpty())
        return;
    //打开的文件加入播放列表并立即播放（支持连续换源）
    playList->addFiles(QStringList{fileName});
    play(fileName);
}

void tmediaplay::onFullScreenBtnClicked()
{
    showFullScreen();
    fullScreenBtn->setVisible(false);
    exitFullBtn->setVisible(true);
    if(playList)
        playList->reposition();
}

void tmediaplay::onExitFullBtnClicked()
{
    showNormal();
    fullScreenBtn->setVisible(true);
    exitFullBtn->setVisible(false);
    if(playList)
        playList->reposition();
}
