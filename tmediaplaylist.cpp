#include "tmediaplaylist.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStandardItemModel>


/*
 * 播放器播放列表类
 * 实现播放列表的查看，文件的添加，删除，拖动等
 * 播放器可以根据播放列表实现单文件播放，循环播放等
 *
 */

TMediaPlayList::TMediaPlayList(QWidget *parent):QDialog(parent)
{
    setWindowTitle("播放列表");
    resize(200,400);

    playListView = new QListView(this); //播放列表
    playListModel = new QStandardItemModel(this);
    playListView->setModel(playListModel);
    //playListView->setSelectionModel(QAbstractItemView::ExtendedSelection);

    // 启用多选
    playListView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    playListView->setSelectionBehavior(QAbstractItemView::SelectRows);

    //启用内部拖拽排序
    playListView->setDragEnabled(true);
    playListView->setAcceptDrops(true);
    playListView->setDragDropMode(QAbstractItemView::InternalMove);
    playListView->setDefaultDropAction(Qt::MoveAction);
    playListView->setDropIndicatorShown(true);

    //添加文件按钮
    addFileBtn = new QPushButton("添加文件",this);
    //删除文件按钮
    removeFileBtn = new QPushButton("移除",this);
    //上移文件按钮
    upFileBtn = new QPushButton("上移",this);
    //下移文件按钮
    downFileBtn = new QPushButton("下移",this);

    //按钮水平分布
    QHBoxLayout *btnHBoxLay = new QHBoxLayout();
    btnHBoxLay->addWidget(addFileBtn);
    btnHBoxLay->addWidget(removeFileBtn);
    btnHBoxLay->addWidget(upFileBtn);
    btnHBoxLay->addWidget(downFileBtn);

    //主分布
    QVBoxLayout *mainVBoxLay = new QVBoxLayout(this);
    mainVBoxLay->addLayout(btnHBoxLay);
    mainVBoxLay->addWidget(playListView);


    //添加事件响应
    connect(addFileBtn, &QPushButton::clicked, this, &TMediaPlayList::onAddFileBtnClicked);
    connect(removeFileBtn, &QPushButton::clicked, this, &TMediaPlayList::onRemoveFileBtnClicked);
    connect(upFileBtn, &QPushButton::clicked, this, &TMediaPlayList::onUpFileBtnClicked);
    connect(downFileBtn, &QPushButton::clicked, this, &TMediaPlayList::onDownFileBtnClicked);

    connect(playListView, &QListView::doubleClicked, this, &TMediaPlayList::onItemDoubleClicked);



}

TMediaPlayList::~TMediaPlayList()
{

}

QStringList TMediaPlayList::playListFiles() const
{

}

void TMediaPlayList::onAddFileBtnClicked()
{
    QString path = QDir::rootPath();
    qDebug()<<"the path is "<< path;
    QString dlgTitle = "打开一个媒体文件";
    QString filter = "媒体文件(*.mp4 *.avi *.mkv);;所有文件(*.*)";
    QStringList fileList = QFileDialog::getOpenFileNames(this, dlgTitle, path, filter);
    //qDebug()<<fileList;
    if(fileList.isEmpty()) return;
    for(const QString &filePath:fileList){
        //根据文件全路径获取文件名
        QFileInfo info(filePath);
        QStandardItem *item = new QStandardItem(info.fileName());
        item->setData(filePath, FilePathRole);

        //开启复选框
        item->setCheckable(true);
        item->setCheckState(Qt::Unchecked);
        playListModel->appendRow(item);
    }

}

void TMediaPlayList::onRemoveFileBtnClicked()
{
    // //获取选中的行对应的models
    // QModelIndexList indexList = playListView->selectionModel()->selectedIndexes();
    // if(indexList.isEmpty())
    //     return; //没有选中的，直接返回
    // //去重并按行号从大到小进行删除，避免删除后行号变化
    // QList<int> rows;
    // for(const QModelIndex &index : indexList){
    //     int row = index.row();
    //     if(!rows.contains(row)){
    //         rows.append(row);
    //     }
    // }
    // std::sort(rows.begin(), rows.end(), [](int a, int b) {
    //     return a > b;
    // });

    // for (int row : rows) {
    //     playListModel->removeRow(row);
    // }

    //修改成勾选项删除
    QList<int> rows;
    //获取播放列表所有项行数
    int count = playListModel->rowCount();
    for(int i = count -1; i>=0; --i){
        QStandardItem *item = playListModel->item(i);
        if(item && item->checkState() == Qt::Checked){
            rows.append(i);
        }
    }
    if(rows.isEmpty()) return;

    for(int row:rows){
        playListModel->removeRow(row);
    }
}

void TMediaPlayList::onUpFileBtnClicked()
{
    //获取选中行
    // QModelIndex currentIdx = playListView->currentIndex();
    // if(!currentIdx.isValid() || currentIdx.row() <=0)
    //     return;
    // int row = currentIdx.row();
    // playListModel->insertRow(row - 1, playListModel->takeItem(row));
    // playListView->setCurrentIndex(playListModel->index(row - 1, 0));
    QModelIndex currentIdx = playListView->currentIndex();
    if(!currentIdx.isValid())
        return;
    int curRow = currentIdx.row();


    if(curRow <= 0)
        return;
    QStandardItem *curItem = playListModel->item(curRow);
    QStandardItem *prevItem = playListModel->item(curRow - 1);

    if(!curItem || !prevItem)
        return;

    //交换显示文本
    QString curText = curItem->text();
    QString prevText = prevItem->text();
    curItem->setText(prevText);
    prevItem->setText(curText);
    //交换文件路径
    QVariant curPath = curItem->data(FilePathRole);
    QVariant prevPath = prevItem->data(FilePathRole);

    curItem->setData(prevPath, FilePathRole);
    prevItem->setData(curPath, FilePathRole);

    //交换勾选状态
    Qt::CheckState curCheck = curItem->checkState();
    Qt::CheckState prevCheck = prevItem->checkState();

    curItem->setCheckState(prevCheck);
    prevItem->setCheckState(curCheck);

    //选中下一项
    playListView->setCurrentIndex(playListModel->index(curRow - 1, 0));


}

void TMediaPlayList::onDownFileBtnClicked()
{
    // QModelIndex currentIndex = playListView->currentIndex();
    // if (!currentIndex.isValid())
    //     return;

    // int row = currentIndex.row();
    // int lastRow = playListModel->rowCount() - 1;
    // //如果已经是最后一行，直接返回
    // if (row >= lastRow)
    //     return;
    // playListModel->insertRow(row + 1, playListModel->takeItem(row));
    // playListView->setCurrentIndex(playListModel->index(row + 1, 0));
    QModelIndex currentIdx = playListView->currentIndex();
    if(!currentIdx.isValid())
        return;
    int curRow = currentIdx.row();
    int lastRow = playListModel->rowCount() - 1;

    if(curRow >= lastRow)
        return;
    QStandardItem *curItem = playListModel->item(curRow);
    QStandardItem *nextItem = playListModel->item(curRow + 1);

    if(!curItem || !nextItem)
        return;

    //交换显示文本
    QString curText = curItem->text();
    QString nextText = nextItem->text();
    curItem->setText(nextText);
    nextItem->setText(curText);
    //交换文件路径
    QVariant curPath = curItem->data(FilePathRole);
    QVariant nextPath = nextItem->data(FilePathRole);

    curItem->setData(nextPath, FilePathRole);
    nextItem->setData(curPath, FilePathRole);

    //交换勾选状态
    Qt::CheckState curCheck = curItem->checkState();
    Qt::CheckState nextCheck = nextItem->checkState();

    curItem->setCheckState(nextCheck);
    nextItem->setCheckState(curCheck);

    //选中下一项
    playListView->setCurrentIndex(playListModel->index(curRow+1, 0));

    QModelIndex index = QModelIndex();
    playListModel->moveRow(index, curRow, index, curRow + 2);
}



void TMediaPlayList::onItemDoubleClicked(const QModelIndex &index)
{
    if(!index.isValid())
        return;

    QString filePath = index.data(FilePathRole).toString();
    //如果文件不为空，发起播放文件信号
    if(!filePath.isEmpty()){
        //等会发起
        emit playFileRequeted(filePath);
    }
}
