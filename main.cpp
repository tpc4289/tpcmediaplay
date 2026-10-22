#include "tmediaplay.h"
#include "tdemux.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    TDemux demux;
    demux.open("E:/jmds03e12.mp4");
    QApplication a(argc, argv);
    tmediaplay w;
    w.show();
    return QApplication::exec();
}
