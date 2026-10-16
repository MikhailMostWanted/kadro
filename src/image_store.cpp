#include "image_store.h"
#include <QtConcurrent>
#include <QTimer>
namespace kadro {
ImageStore::ImageStore(int budgetMiB, int maxQueue, QObject* parent)
    : QObject(parent), cache_(budgetMiB * 1024), maxQueue_(maxQueue) {
  pool_.setMaxThreadCount(1);
  pool_.setExpiryTimeout(10000);
  connect(&watcher_, &QFutureWatcher<Decoded>::finished, this, [this] {
    const auto done = active_;
    auto value = watcher_.result();
    if (imageKey(done.path,done.edge)!=done.key) { value.image={}; value.error="Файл изменился во время чтения. Откройте его ещё раз."; }
    busy_ = false;
    // Never insert a stale file into the cache under its newer on-disk identity.
    if (imageKey(done.path, done.edge) == done.key) {
      int kib = int((value.image.sizeInBytes() + 1023) / 1024);
      cache_.insert(done.key, new Decoded(value), qMax(1, kib));
    }
    emit ready(done.path, done.edge, value);
    pump();
  });
}
ImageStore::~ImageStore() {
  disconnect(&watcher_, nullptr, this, nullptr);
  queue_.clear();
  pool_.waitForDone(); // workers own values only, no widget access
}
const Decoded* ImageStore::cached(const QString& path, int edge) { return cache_.object(imageKey(path, edge)); }
void ImageStore::clearPending() { queue_.clear(); }
void ImageStore::request(const QString& path, int edge, bool urgent) {
  const auto key = imageKey(path, edge);
  if (urgent) queue_.clear();
  if (const auto* hit = cache_.object(key)) { const auto copy = *hit; QTimer::singleShot(0,this,[this,path,edge,copy]{emit ready(path,edge,copy);}); return; }
  if (busy_ && active_.key == key) return;
  for (const auto& job : queue_) if (job.key == key) return;
  if (int(queue_.size()) >= maxQueue_) {
    if (!urgent) return;
    queue_.pop_back();
  }
  Job job{path,key,edge};
  if (urgent) queue_.push_front(job); else queue_.push_back(job);
  pump();
}
void ImageStore::pump() {
  if (busy_ || queue_.empty()) return;
  active_ = queue_.front(); queue_.pop_front(); busy_ = true;
  const auto job = active_;
  watcher_.setFuture(QtConcurrent::run(&pool_, [job] { return decodeImage(job.path, job.edge); }));
}
}
