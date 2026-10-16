#include <climits>
#include "desktop.h"
#include "core/media.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QProcess>
#include <QSettings>
#include <QUrl>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif
namespace kadro::desktop {
bool reveal(const QString& path) {
  if (!QFileInfo::exists(path)) return false;
#ifdef Q_OS_WIN
  const auto native=QDir::toNativeSeparators(path).toStdWString();
  PIDLIST_ABSOLUTE id=nullptr;
  HRESULT hr=SHParseDisplayName(native.c_str(),nullptr,&id,0,nullptr);
  if (SUCCEEDED(hr)) { hr=SHOpenFolderAndSelectItems(id,0,nullptr,0); CoTaskMemFree(id); return SUCCEEDED(hr); }
  return false;
#elif defined(Q_OS_MACOS)
  return QProcess::startDetached("/usr/bin/open",{"-R",path});
#else
  return QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}
bool copyFile(const QString& path) {
  if (!QFileInfo(path).isFile()) return false;
  auto* mime=new QMimeData; mime->setUrls({QUrl::fromLocalFile(path)});
  QApplication::clipboard()->setMimeData(mime); return true;
}
QString renameFile(const QString& path,const QString& newName,QString* resultingPath) {
  const auto error=nameError(newName.toUtf8().toStdString());
  if (!error.empty()) return QString::fromUtf8(error.c_str());
  QFileInfo old(path);
  if (!old.isFile()) return "Файл больше не существует.";
  const QString target=old.dir().absoluteFilePath(newName);
  if (target==path) { if (resultingPath) *resultingPath=path; return {}; }
  if (QFileInfo::exists(target)) return "Файл с таким именем уже существует. Он не будет перезаписан.";
  QFile file(path);
  if (!file.rename(target)) return "Не удалось переименовать файл: "+file.errorString();
  if (resultingPath) *resultingPath=target;
  return {};
}
QString trashFile(const QString& path) {
  QFile file(path);
  if (!file.exists()) return "Файл больше не существует.";
  if (!file.moveToTrash()) return "Не удалось переместить файл в корзину: "+file.errorString()+" Оригинал оставлен на месте.";
  return {};
}
bool registerViewer() {
#ifdef Q_OS_WIN
  const QString exe=QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
  const QString command='"'+exe+"\" \"%1\"";
  QSettings classes("HKEY_CURRENT_USER\\Software\\Classes",QSettings::NativeFormat);
  classes.setValue("Kadro.Media/Default","Медиафайл KADRO");
  classes.setValue("Kadro.Media/shell/open/command/Default",command);
  classes.setValue("Applications/Kadro.exe/FriendlyAppName","KADRO");
  classes.setValue("Applications/Kadro.exe/shell/open/command/Default",command);
  QSettings cap("HKEY_CURRENT_USER\\Software\\Kadro\\Capabilities",QSettings::NativeFormat);
  cap.setValue("ApplicationName","KADRO"); cap.setValue("ApplicationDescription","Просмотр фотографий и видео");
  const QStringList extensions={"jpg","jpeg","png","gif","bmp","webp","tif","tiff","heic","heif","avif","ico","mp4","m4v","mov","mkv","webm","avi","wmv","mpg","mpeg","mts","m2ts","3gp"};
  for (const auto& ext:extensions) {
    classes.setValue('.'+ext+"/OpenWithProgids/Kadro.Media",QString());
    classes.setValue("Applications/Kadro.exe/SupportedTypes/."+ext,QString());
    cap.setValue("FileAssociations/."+ext,"Kadro.Media");
  }
  QSettings registered("HKEY_CURRENT_USER\\Software\\RegisteredApplications",QSettings::NativeFormat);
  registered.setValue("KADRO","Software\\Kadro\\Capabilities");
  classes.sync(); cap.sync(); registered.sync();
  SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
  return classes.status()==QSettings::NoError && cap.status()==QSettings::NoError && registered.status()==QSettings::NoError;
#else
  return true;
#endif
}
QString defaultAppInstructions() {
#ifdef Q_OS_WIN
  return "KADRO добавлен в список приложений. В параметрах Windows выберите KADRO для нужных расширений. Выбор по умолчанию остаётся за вами. Не перемещайте папку приложения после регистрации.";
#elif defined(Q_OS_MACOS)
  return "Перенесите KADRO.app в «Программы». В Finder выберите файл → «Свойства» → «Открывать в программе» → KADRO → «Настроить… / Изменить всё». Повторите для нужных типов файлов.";
#else
  return "Выберите KADRO через «Открыть с помощью» в файловом менеджере.";
#endif
}
#ifdef Q_OS_WIN
QImage readNativeImage(const QString& path,int edge,QSize* original) {
  using Microsoft::WRL::ComPtr;
  const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
  struct Uninit { bool active; ~Uninit(){ if(active) CoUninitialize(); } } guard{SUCCEEDED(init)};
  if (FAILED(init) && init!=RPC_E_CHANGED_MODE) return {};
  ComPtr<IWICImagingFactory> factory;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))) return {};
  ComPtr<IWICBitmapDecoder> decoder;
  const auto native=QDir::toNativeSeparators(path).toStdWString();
  if (FAILED(factory->CreateDecoderFromFilename(native.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder))) return {};
  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(decoder->GetFrame(0,&frame))) return {};
  UINT w=0,h=0; if (FAILED(frame->GetSize(&w,&h)) || !w || !h || w>INT_MAX || h>INT_MAX) return {};
  *original=QSize(int(w),int(h));
  const auto [sw,sh]=boundedSize(int(w),int(h),edge,24000000);
  ComPtr<IWICBitmapScaler> scaled;
  if (FAILED(factory->CreateBitmapScaler(&scaled)) || FAILED(scaled->Initialize(frame.Get(),UINT(sw),UINT(sh),WICBitmapInterpolationModeFant))) return {};
  ComPtr<IWICFormatConverter> converted;
  if (FAILED(factory->CreateFormatConverter(&converted)) || FAILED(converted->Initialize(scaled.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return {};
  QImage image(sw,sh,QImage::Format_ARGB32_Premultiplied);
  if (image.isNull() || FAILED(converted->CopyPixels(nullptr,UINT(image.bytesPerLine()),UINT(image.sizeInBytes()),image.bits()))) return {};
  return image;
}
#endif
}
