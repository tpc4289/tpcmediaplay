#include "tmediaplay.h"
#include "ui_tmediaplay.h"

tmediaplay::tmediaplay(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::tmediaplay)
{
    ui->setupUi(this);
}

tmediaplay::~tmediaplay()
{
    delete ui;
}
