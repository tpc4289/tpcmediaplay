#include "tmediaplay.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    // TDemux demux;
    // demux.open("E:/jmds03e12.mp4");
    // demux.open("G:/download/BitComet/ffxxz35.mp4");
    // TDemuxThread *demuxThread = new TDemuxThread();
    // demuxThread->openMedia("E:/jmds03e12.mp4");
    // demuxThread->start();
    QApplication a(argc, argv);
    tmediaplay w;
    w.show();
    w.play("G:/download/BitComet/ffxxz35.mp4");
    return QApplication::exec();
}
