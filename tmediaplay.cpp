#include "tmediaplay.h"
#include "ui_tmediaplay.h"
#include "tdemuxthread.h"
#include "tdecode.h"
#include "tvideowidget.h"
#include "tvideothread.h"
#include "taudiothread.h"

#include <QVBoxLayout>

tmediaplay::tmediaplay(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::tmediaplay)
{
    ui->setupUi(this);
    setWindowTitle("tpcmediaplay播放器");
    resize(900,640);

    videoWidget = new TVideoWidget(this);
    QVBoxLayout *vBoxLay = new QVBoxLayout(this);
    vBoxLay->addWidget(videoWidget);
    setLayout(vBoxLay);


}

tmediaplay::~tmediaplay()
{
    //关闭顺序很重要：先停"生产者"（解封装），再停"消费者"（音视频线程），
    //最后才释放它们引用的对象（解码器/队列/设备）。
    //原实现只 delete ui，关窗时线程还在跑并访问即将析构的对象 → 崩溃/退出卡死
    if(demuxThread){
        demuxThread->requestInterruption();
        //停队列唤醒可能卡在 push/pop 上的线程
        demuxThread->getVideoPackQueue()->stop();
        demuxThread->getAudioPackQueue()->stop();
        demuxThread->wait();
    }
    //线程对象析构内部会 requestInterruption + wait，这里显式先停更安全
    delete videoThread;   videoThread = nullptr;
    delete audioThread;   audioThread = nullptr;
    delete demuxThread;   demuxThread = nullptr;

    delete videoDecode;   videoDecode = nullptr;
    delete audioDecode;   audioDecode = nullptr;

    delete ui;
    ui = nullptr;
}

void tmediaplay::play(const QString &url)
{
    //解封装线程
    demuxThread = new TDemuxThread();
    //打开失败（路径错/无视频流）则不启动任何线程，界面保持黑底并给出提示
    if (!demuxThread->openMedia(url.toUtf8().constData())) {
        qDebug() << "open failed:" << url;
        delete demuxThread;
        demuxThread = nullptr;
        return;
    }
    TDemux *demux = demuxThread->getTDemux();


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
    audioThread->init(audioDecode, demuxThread->getAudioPackQueue());
    qDebug() << "audio packet count is "<<demuxThread->getAudioPackQueue()->getCount();

    //视频解码显示
    videoThread = new TVideoThread();
    //关键接线：把音频设备（主时钟）交给视频线程，sync() 才有依据
    videoThread->init(videoDecode, demuxThread->getVideoPackQueue(), demux->getFps());
    qDebug() << "video packet count is "<<demuxThread->getVideoPackQueue()->getCount();
    //跨线程送显：TVideoThread 运行在工作线程、TVideoWidget 在主线程，
    //必须显式队列连接（参数按值拷贝）。不写会退化为 DirectConnection 风险写法，
    //在解码线程里直接调 update() 属于跨线程操作 GUI，未定义行为
    connect(videoThread, &TVideoThread::frameReady, videoWidget, &TVideoWidget::slotRepaint, Qt::QueuedConnection);


    //启动顺序：先消费者（音/视频线程），再生产者（解封装），
    //避免解封装抢跑把 3MB 队列塞满后卡在背压上
    audioThread->start();
    videoThread->start();
    demuxThread->start();




}
