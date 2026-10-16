#include <QImageIOHandler>
#include "decoder.h"
#include "core/media.h"
#include "platform/desktop.h"
#include <QColorSpace>
#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
namespace kadro {
QString imageKey(const QString& path, int edge) {
  QFileInfo f(path);
  return path + QChar(0x1f) + QString::number(f.size()) + ':' + QString::number(f.lastModified().toMSecsSinceEpoch()) + ':' + QString::number(edge);
}
Decoded decodeImage(const QString& path, int edge) {
  Decoded out;
  QImageReader reader(path);
  reader.setAutoTransform(true);
  out.original = reader.size();
  out.animated = reader.supportsAnimation();
  if (out.original.isValid()) {
    const auto [w,h] = boundedSize(out.original.width(), out.original.height(), edge, 24000000);
    if (QSize(w,h) != out.original) reader.setScaledSize(QSize(w,h));
  }
  out.image = reader.read();
#ifdef Q_OS_WIN
  // WIC discovers the user's installed HEIF/AVIF codec, without downloading one.
  auto suffix = QFileInfo(path).suffix().toLower();
  if (out.image.isNull() && (suffix == "heic" || suffix == "heif" || suffix == "avif"))
    out.image = desktop::readNativeImage(path, edge, &out.original);
#endif
  if (out.image.isNull()) {
    out.error = "Не удалось открыть изображение. " + reader.errorString();
    auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "heic" || suffix == "heif" || suffix == "avif")
      out.error += " Для этого формата нужен подходящий системный кодек или плагин Qt. Оригинал не изменён.";
    return out;
  }
  // Defend against plugins that ignore scaled decoding. Peak allocation is also
  // bounded by QImageReader's process-wide allocation limit set in main().
  auto [w,h] = boundedSize(out.image.width(), out.image.height(), edge, 24000000);
  if (QSize(w,h) != out.image.size()) out.image = out.image.scaled(w,h,Qt::KeepAspectRatio,Qt::SmoothTransformation);
  if (!out.original.isValid()) out.original = out.image.size();
  // QImageReader applies EXIF orientation. Track oriented dimensions as well.
  if (reader.transformation() & QImageIOHandler::TransformationRotate90) out.original.transpose();
  if (out.image.colorSpace().isValid() && out.image.colorSpace() != QColorSpace(QColorSpace::SRgb))
    out.image.convertToColorSpace(QColorSpace::SRgb);
  out.image = out.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  return out;
}
}
