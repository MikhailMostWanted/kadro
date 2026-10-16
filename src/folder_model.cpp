#include "folder_model.h"
#include <QApplication>
#include <QDateTime>
#include <QDirIterator>
#include <QFileInfo>
#include <QIcon>
#include <QPixmap>
#include <QStyle>
#include <QtConcurrent>
namespace kadro {
FolderModel::FolderModel(QObject* parent) : QAbstractListModel(parent), thumbnails_(new ImageStore(24,48,this)) {
  connect(thumbnails_, &ImageStore::ready, this, [this](const QString& path, int, const Decoded&) {
    const int row = rows_.value(path, -1);
    if (row >= 0) emit dataChanged(index(row),index(row),{Qt::DecorationRole});
  });
}
FolderModel::~FolderModel() { if (cancel_) *cancel_ = true; }
int FolderModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(entries_.size()); }
QVariant FolderModel::data(const QModelIndex& idx, int role) const {
  if (!idx.isValid() || idx.row() < 0 || idx.row() >= entries_.size()) return {};
  const auto& e = entries_[idx.row()];
  switch (role) {
    case Qt::DisplayRole: return e.name;
    case Qt::ToolTipRole: return e.path;
    case PathRole: return e.path;
    case KindRole: return int(e.kind);
    case BytesRole: return e.bytes;
    case ModifiedRole: return e.modified;
    case Qt::DecorationRole:
      if (e.kind == Kind::Video) return QApplication::style()->standardIcon(QStyle::SP_MediaPlay);
      if (const auto* hit = thumbnails_->cached(e.path,160)) return QIcon(QPixmap::fromImage(hit->image));
      thumbnails_->request(e.path,160);
      return QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    default: return {};
  }
}
void FolderModel::scan(const QString& folder) {
  if (cancel_) *cancel_ = true;
  cancel_ = std::make_shared<std::atomic_bool>(false);
  const auto token = cancel_;
  const auto gen = ++generation_;
  folder_ = folder; scanning_ = true;
  thumbnails_->clearPending();
  beginResetModel(); entries_.clear(); rows_.clear(); endResetModel();
  using Result = std::pair<QVector<Entry>, QString>;
  auto* watcher = new QFutureWatcher<Result>(this);
  connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,gen] {
    auto result = watcher->result(); watcher->deleteLater();
    if (gen != generation_) return;
    scanning_ = false;
    beginResetModel(); entries_ = std::move(result.first);
    for (int i=0;i<entries_.size();++i) rows_.insert(entries_[i].path,i);
    endResetModel();
    if (!result.second.isEmpty()) emit scanFailed(result.second);
    emit scanFinished();
  });
  watcher->setFuture(QtConcurrent::run([folder,token]() -> Result {
    QVector<Entry> entries;
    if (!QFileInfo(folder).isDir() || !QFileInfo(folder).isReadable()) return {entries,"Папка недоступна или нет разрешения на чтение."};
    QDirIterator it(folder,QDir::Files | QDir::Readable | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
      if (token->load()) return {};
      it.next(); const QFileInfo f = it.fileInfo();
      const auto kind = kindForExtension(f.suffix().toStdString());
      if (kind == Kind::Unknown) continue;
      entries.push_back({f.absoluteFilePath(),f.fileName(),kind,f.size(),f.lastModified().toMSecsSinceEpoch()});
    }
    std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b) { return naturalLess(a.name.toStdString(),b.name.toStdString()); });
    return {std::move(entries),{}};
  }));
}
MediaFilter::MediaFilter(QObject* parent) : QSortFilterProxyModel(parent) { setDynamicSortFilter(true); sort(0); }
void MediaFilter::setKind(Kind kind) { kind_ = kind; invalidateFilter(); }
void MediaFilter::setOrder(Sort value) { order_ = value; invalidate(); sort(0); }
bool MediaFilter::filterAcceptsRow(int row,const QModelIndex& parent) const {
  return kind_ == Kind::Unknown || sourceModel()->index(row,0,parent).data(FolderModel::KindRole).toInt() == int(kind_);
}
bool MediaFilter::lessThan(const QModelIndex& a,const QModelIndex& b) const {
  if (order_ != Sort::Name) {
    int role = order_ == Sort::Size ? FolderModel::BytesRole : FolderModel::ModifiedRole;
    auto x = a.data(role).toLongLong(), y = b.data(role).toLongLong();
    if (x != y) return order_ == Sort::Oldest ? x < y : x > y;
  }
  return naturalLess(a.data().toString().toStdString(),b.data().toString().toStdString());
}
}
