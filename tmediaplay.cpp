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
    delete ui;
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
        return;
    }

    //videoDecode->send();

    //音频解码器
    AVCodecParameters *audioCodecPara = demux->copyAudioPara();
    //音频流可能为空，需要判断
    if(audioCodecPara){
        audioDecode = new TDecode();
        audioDecode->setIsAudio(true);
        //打开
        if(!audioDecode->open(audioCodecPara))
            qDebug()<<"audio decoder open failed!";
    }

    //视频解码显示
    videoThread = new TVideoThread();
    videoThread->init(videoDecode, demuxThread->getVideoPackQueue(), demux->getFps());
    qDebug() << "video packet count is "<<demuxThread->getVideoPackQueue()->getCount();
    connect(videoThread, &TVideoThread::frameReady, videoWidget, &TVideoWidget::slotRepaint);

    //音频重采样播放线程
    audioThread = new TAudioThread();
    audioThread->init(audioDecode, demuxThread->getAudioPackQueue());
    qDebug() << "audio packet count is "<<demuxThread->getAudioPackQueue()->getCount();
    audioThread->start();
    videoThread->start();
    demuxThread->start();




}
