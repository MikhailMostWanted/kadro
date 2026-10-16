#pragma once
#include <QImage>
#include <QString>
namespace kadro::desktop {
bool reveal(const QString& path);
bool copyFile(const QString& path);
QString renameFile(const QString& path,const QString& newName,QString* resultingPath);
QString trashFile(const QString& path);
bool registerViewer();
QString defaultAppInstructions();
#ifdef Q_OS_WIN
QImage readNativeImage(const QString& path,int edge,QSize* original);
#endif
}
