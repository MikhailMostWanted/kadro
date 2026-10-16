#include <QFileInfo>
#include "ui/window.h"
#include "platform/single_instance.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFileOpenEvent>
#include <QImageReader>
#include <QMessageBox>
#include <QTimer>
#include <QTemporaryDir>
#include <QSettings>
#include <memory>
#include <functional>
#ifdef Q_OS_WIN
#include <objbase.h>
#endif
class Application final : public QApplication {
public:
  using QApplication::QApplication;
  std::function<void(QString)> open;
protected:
  bool event(QEvent* e) override {
    if(e->type()==QEvent::FileOpen){
      auto* f=static_cast<QFileOpenEvent*>(e);
      if(open && f->url().isLocalFile())open(f->file());
      return true;
    }
    return QApplication::event(e);
  }
};
int main(int argc,char** argv) {
#ifdef Q_OS_WIN
  const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  struct Uninitialize { HRESULT hr; ~Uninitialize(){if(SUCCEEDED(hr))CoUninitialize();} } uninitialize{com};
#endif
  Application app(argc,argv);
  QCoreApplication::setOrganizationName("Kadro"); QCoreApplication::setApplicationName("Kadro"); QCoreApplication::setApplicationVersion("0.1.0");
  QApplication::setApplicationDisplayName("KADRO"); QApplication::setStyle("Fusion");
  QImageReader::setAllocationLimit(128);
  QCommandLineParser parser; parser.setApplicationDescription("KADRO — просмотр фотографий и видео"); parser.addHelpOption(); parser.addVersionOption(); parser.addPositionalArgument("file","Фото, видео или папка","[file]");
  QCommandLineOption isolated("isolated","Отдельный процесс (для диагностики)"); parser.addOption(isolated);
  QCommandLineOption screenshot("smoke-screenshot","Сохранить снимок окна и выйти (диагностика)","path"); parser.addOption(screenshot);
  parser.process(app);
  if(parser.positionalArguments().size()>1)parser.showHelp(2);
  const QString path=parser.positionalArguments().isEmpty()?QString():QFileInfo(parser.positionalArguments().first()).absoluteFilePath();
  std::unique_ptr<QTemporaryDir> smokeSettings;
  if(parser.isSet(screenshot)) {
    smokeSettings=std::make_unique<QTemporaryDir>();
    if(!smokeSettings->isValid())return 5;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,smokeSettings->path());
  }
  kadro::SingleInstance instance;
  if(!parser.isSet(isolated)) {
    QString error; int status=instance.start(path,&error);
    if(status==1)return 0;
    if(status<0){QMessageBox::warning(nullptr,"KADRO",error);return 1;}
  }
  kadro::Window window;
  app.open=[&window](const QString& file){window.openPath(file);window.show();window.raise();window.activateWindow();};
  QObject::connect(&instance,&kadro::SingleInstance::openRequested,&window,[&](const QString& file){app.open(file);});
  window.show();
  if(!path.isEmpty())QTimer::singleShot(0,&window,[&window,path]{window.openPath(path);});
  QElapsedTimer smokeElapsed;
  if(parser.isSet(screenshot)) {
    smokeElapsed.start();
    auto* probe=new QTimer(&window); probe->setInterval(100);
    QObject::connect(probe,&QTimer::timeout,&window,[&window,&app,&parser,&path,&smokeElapsed]{
      // A screenshot of an error/empty page is not a successful media smoke test.
      if(!path.isEmpty() && !window.hasDecodedMedia()) {
        if(smokeElapsed.elapsed()<15000)return;
        window.grab().save(parser.value("smoke-screenshot"));app.exit(4);return;
      }
      bool ok=window.grab().save(parser.value("smoke-screenshot"));app.exit(ok?0:3);
    });
    probe->start();
  }
  return app.exec();
}
