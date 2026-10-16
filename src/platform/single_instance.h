#pragma once
#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <memory>
namespace kadro {
class SingleInstance : public QObject {
  Q_OBJECT
public:
  explicit SingleInstance(QObject* parent=nullptr);
  // 0 primary, 1 delivered to primary, -1 error. No TCP port, no remote access.
  int start(const QString& path,QString* error);
signals:
  void openRequested(const QString& path);
private:
  QLocalServer server_;
  std::unique_ptr<QLockFile> lock_;
};
}
