#include <QThread>
#include "single_instance.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTimer>
namespace kadro {
SingleInstance::SingleInstance(QObject* parent):QObject(parent) {
  server_.setSocketOptions(QLocalServer::UserAccessOption);
  connect(&server_,&QLocalServer::newConnection,this,[this]{
    while (auto* socket=server_.nextPendingConnection()) {
      socket->setReadBufferSize(65537);
      auto buffer=std::make_shared<QByteArray>();
      connect(socket,&QLocalSocket::readyRead,this,[this,socket,buffer]{
        buffer->append(socket->readAll());
        if (buffer->size()>65536) { socket->abort(); return; }
        const auto end=buffer->indexOf('\n'); if (end<0) return;
        QJsonParseError error;
        auto doc=QJsonDocument::fromJson(buffer->left(end),&error);
        if (error.error!=QJsonParseError::NoError || !doc.isObject() || !doc.object().value("path").isString()) { socket->abort(); return; }
        const QString path=doc.object().value("path").toString();
        if (!path.isEmpty() && !QFileInfo(path).isAbsolute()) { socket->abort(); return; }
        emit openRequested(path);
        socket->write("OK\n"); socket->flush(); socket->disconnectFromServer();
      });
      connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
      QTimer::singleShot(3000,socket,[socket]{ socket->abort(); socket->deleteLater(); });
    }
  });
}
int SingleInstance::start(const QString& path,QString* error) {
  const QString root=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (!QDir().mkpath(root)) { *error="Не удалось создать папку настроек KADRO."; return -1; }
  const QString suffix=QString::fromLatin1(QCryptographicHash::hash(root.toUtf8(),QCryptographicHash::Sha256).toHex().left(24));
  const QString name="kadro-"+suffix;
  lock_=std::make_unique<QLockFile>(root+"/instance.lock"); lock_->setStaleLockTime(0);
  if (!lock_->tryLock(0)) {
    // The first process can own the lock before its socket is listening.
    for (int retry=0;retry<5;++retry) {
      QLocalSocket socket; socket.connectToServer(name);
      if (!socket.waitForConnected(250)) { QThread::msleep(100); continue; }
      socket.write(QJsonDocument(QJsonObject{{"path",path}}).toJson(QJsonDocument::Compact)+'\n');
      socket.flush(); socket.waitForBytesWritten(500);
      if (socket.bytesAvailable()>0 || socket.waitForReadyRead(1000)) {
        if (socket.readAll().startsWith("OK")) return 1;
      }
    }
    *error="KADRO уже запущен, но не ответил. Закройте его окно и повторите открытие."; return -1;
  }
  QLocalServer::removeServer(name); // safe: the per-user lock is held
  if (!server_.listen(name)) { *error=server_.errorString(); return -1; }
  return 0;
}
}
