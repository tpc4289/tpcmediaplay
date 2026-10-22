#include "tmediaplay.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    tmediaplay w;
    w.show();
    return QApplication::exec();
}
