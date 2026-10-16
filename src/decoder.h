#pragma once
#include <QImage>
#include <QSize>
#include <QString>
namespace kadro {
struct Decoded {
  QImage image;
  QSize original;
  QString error;
  bool animated = false;
};
// edge == 0 requests native resolution, still subject to the pixel budget.
Decoded decodeImage(const QString& path, int edge);
QString imageKey(const QString& path, int edge);
}
