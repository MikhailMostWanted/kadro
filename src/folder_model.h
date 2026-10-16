#pragma once
#include "core/media.h"
#include "image_store.h"
#include <QAbstractListModel>
#include <QHash>
#include <QSortFilterProxyModel>
#include <atomic>
#include <memory>
namespace kadro {
struct Entry { QString path, name; Kind kind; qint64 bytes = 0, modified = 0; };
class FolderModel : public QAbstractListModel {
  Q_OBJECT
public:
  enum Role { PathRole = Qt::UserRole + 1, KindRole, BytesRole, ModifiedRole };
  explicit FolderModel(QObject* parent = nullptr);
  ~FolderModel() override;
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  void scan(const QString& folder);
  QString folder() const { return folder_; }
  int rowForPath(const QString& path) const { return rows_.value(path, -1); }
  bool isScanning() const { return scanning_; }
signals:
  void scanFinished();
  void scanFailed(const QString& message);
private:
  QVector<Entry> entries_;
  QHash<QString, int> rows_;
  ImageStore* thumbnails_;
  QString folder_;
  std::shared_ptr<std::atomic_bool> cancel_;
  quint64 generation_ = 0;
  bool scanning_ = false;
};
class MediaFilter : public QSortFilterProxyModel {
  Q_OBJECT
public:
  enum class Sort { Name, Newest, Oldest, Size };
  explicit MediaFilter(QObject* parent = nullptr);
  void setKind(Kind kind);
  Kind kind() const { return kind_; }
  void setOrder(Sort sort);
  Sort order() const { return order_; }
protected:
  bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
  bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
private:
  Kind kind_ = Kind::Unknown;
  Sort order_ = Sort::Name;
};
}
