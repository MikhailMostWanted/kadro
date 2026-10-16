#pragma once
#include "decoder.h"
#include <QCache>
#include <QFutureWatcher>
#include <QObject>
#include <QThreadPool>
#include <deque>
namespace kadro {
// A bounded, serial decode queue. The viewer and filmstrip own separate stores,
// so thumbnails can never consume the foreground decoder's queue.
class ImageStore : public QObject {
  Q_OBJECT
public:
  explicit ImageStore(int budgetMiB, int maxQueue, QObject* parent = nullptr);
  ~ImageStore() override;
  void request(const QString& path, int edge, bool urgent = false);
  const Decoded* cached(const QString& path, int edge);
  void clearPending();
  int queuedCount() const { return int(queue_.size()); }
  int cacheCost() const { return cache_.totalCost(); }
signals:
  void ready(const QString& path, int edge, const kadro::Decoded& decoded);
private:
  struct Job { QString path, key; int edge; };
  void pump();
  QCache<QString, Decoded> cache_;
  QThreadPool pool_;
  QFutureWatcher<Decoded> watcher_;
  std::deque<Job> queue_;
  Job active_;
  int maxQueue_;
  bool busy_ = false;
};
}
