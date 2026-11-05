#include "platform/settings_profile.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool ok,const char* description) {
  ++checks;
  if(!ok)throw std::runtime_error(description);
}
QByteArray bytes(const QString& path) {
  QFile file(path);require(file.open(QIODevice::ReadOnly),"read owned fixture");
  return file.readAll();
}
void write(const QString& path,const QByteArray& value) {
  QFile file(path);require(file.open(QIODevice::WriteOnly),"create owned fixture");
  require(file.write(value)==value.size(),"write owned fixture");
}
QByteArray digest(const QString& path) {
  return QCryptographicHash::hash(bytes(path),QCryptographicHash::Sha256);
}
bool parseIsolated(const QStringList& args) {
  QCommandLineParser parser;kadro::addDiagnosticOptions(parser);
  require(parser.parse(args),"parse diagnostic command line");
  return kadro::diagnosticInstance(parser);
}
}

int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  QCoreApplication::setOrganizationName("Kadro");
  QCoreApplication::setApplicationName("Kadro");
  try {
    const auto originalFormat=QSettings::defaultFormat();
    require(!parseIsolated({"Kadro","photo.png"}),"normal launch does not imply isolation");
    require(parseIsolated({"Kadro","--isolated"}),"legacy isolated option retained");
    require(parseIsolated({"Kadro","--settings-dir","/fixture/profile"}),"explicit profile implies isolated IPC");
    require(parseIsolated({"Kadro","--smoke-screenshot","capture.png"}),"smoke cannot contact normal IPC");
    require(parseIsolated({"Kadro","--settings-dir","/fixture/profile","--smoke-screenshot","capture.png"}),"explicit profile and smoke coexist");
    QCommandLineParser missing;kadro::addDiagnosticOptions(missing);
    require(!missing.parse({"Kadro","--settings-dir"}),"missing directory is a parse error");
    require(QSettings::defaultFormat()==originalFormat,"parsing never changes default format");
    QTemporaryDir temp;require(temp.isValid(),"own temporary root");
    const QString invalidFile=temp.filePath("ordinary-file");write(invalidFile,"keep");
    QString error;
    require(!kadro::configureSettingsProfile(invalidFile,&error),"file rejected before initial configuration");
    require(!error.isEmpty(),"invalid destination has a clear error");
    require(QSettings::defaultFormat()==originalFormat,"invalid destination leaves initial format unchanged");

    // Never instantiate host NativeFormat settings. All four fallback levels
    // are seeded under a synthetic normal profile before any QSettings object.
    const QString normal=temp.filePath("normal");
    require(QDir().mkpath(normal+"/user"),"create synthetic user path");
    require(QDir().mkpath(normal+"/system"),"create synthetic system path");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,normal+"/user");
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,normal+"/system");
    QStringList normalFiles,keys;QList<QByteArray> hashes;
    for(const auto scope : {QSettings::UserScope,QSettings::SystemScope}) {
      for(const auto& application : {QString("Kadro"),QString()}) {
        QSettings settings(QSettings::IniFormat,scope,"Kadro",application);
        const QString key=QString("sentinel/%1/%2").arg(scope).arg(application.isEmpty()?"organisation":"application");
        settings.setValue(key,"normal-only");settings.sync();
        require(settings.status()==QSettings::NoError,"persist synthetic fallback sentinel");
        normalFiles.append(settings.fileName());keys.append(key);hashes.append(digest(settings.fileName()));
      }
    }
    { QSettings normalSettings;
      for(const auto& key : keys)require(normalSettings.contains(key),"normal fixture exercises every fallback level");
    }

    const QString selected=temp.filePath(QString::fromUtf8("профиль с пробелом"));
    require(kadro::configureSettingsProfile(selected,&error),"select isolated profile");
    require(error.isEmpty(),"success clears earlier error");
    require(QSettings::defaultFormat()==QSettings::IniFormat,"isolated format is INI");
    const QString canonical=QDir(selected).canonicalPath();
    QString userFile,systemFile;
    { QSettings settings;
      require(settings.allKeys().isEmpty(),"no normal user/system/organisation fallback leaks");
      for(const auto& key : keys)require(!settings.contains(key),"no normal sentinel readable");
      userFile=settings.fileName();
      require(QDir::fromNativeSeparators(userFile).startsWith(canonical+"/user/"),"user file inside owned profile");
      settings.setValue("view/light",true);settings.setValue("video/volume",37);
      settings.setValue("video/resume",true);settings.setValue("video/positions/test",5000);
      settings.setValue("window/geometry",QByteArray("owned-geometry"));settings.sync();
      require(settings.status()==QSettings::NoError,"persist application keys");
    }
    { QSettings settings(QSettings::IniFormat,QSettings::SystemScope,"Kadro","Kadro");
      systemFile=settings.fileName();
      require(QDir::fromNativeSeparators(systemFile).startsWith(canonical+"/system/"),"system file inside owned profile");
      require(settings.allKeys().isEmpty(),"system settings do not read normal fallback");
      settings.setValue("contained/system",true);settings.sync();
      require(settings.status()==QSettings::NoError,"persist contained system sentinel");
    }
    { QSettings restored;
      require(restored.value("view/light").toBool(),"theme persisted");
      require(restored.value("video/volume").toInt()==37,"volume persisted");
      require(restored.value("video/positions/test").toInt()==5000,"resume position persisted");
      require(restored.value("window/geometry").toByteArray()=="owned-geometry","geometry persisted");
      require(restored.value("contained/system").toBool(),"allowed fallback remains contained in owned system path");
    }
    const QByteArray ini=bytes(userFile);
    require(ini.contains("volume=37")&&ini.contains("owned-geometry"),"actual INI bytes contain persisted keys");
    const auto destinationUnchanged=[&] {
      QSettings current;QSettings system(QSettings::IniFormat,QSettings::SystemScope,"Kadro","Kadro");
      require(current.fileName()==userFile && system.fileName()==systemFile,"both destinations unchanged after rejection");
      require(current.value("video/positions/test").toInt()==5000,"previous profile still readable after rejection");
      require(QSettings::defaultFormat()==QSettings::IniFormat,"format unchanged after rejection");
    };
    const QString blocked=temp.filePath("blocked");require(QDir().mkpath(blocked),"create blocked fixture");
    write(blocked+"/system","not a directory");
    for(const auto& invalid : {QString(),QString("relative-profile"),invalidFile,QDir::rootPath(),blocked}) {
      require(!kadro::configureSettingsProfile(invalid,&error),"invalid profile rejected");
      require(!error.isEmpty(),"rejected profile error is nonempty");destinationUnchanged();
    }
#ifdef Q_OS_UNIX
    const QString linked=temp.filePath("linked");require(QDir().mkpath(linked),"create symlink fixture");
    require(QFile::link(normal+"/system",linked+"/system"),"make only owned symlink");
    require(!kadro::configureSettingsProfile(linked,&error),"scope symlink cannot escape profile");destinationUnchanged();
    const QString readOnly=temp.filePath("readonly");require(QDir().mkpath(readOnly),"create readonly fixture");
    require(QFile::setPermissions(readOnly,QFile::ReadOwner|QFile::ExeOwner),"restrict own fixture permissions");
    const bool denied=!QFileInfo(readOnly).isWritable();
    const bool configured=denied?kadro::configureSettingsProfile(readOnly,&error):false;
    require(QFile::setPermissions(readOnly,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner),"restore own fixture permissions");
    if(denied){require(!configured,"unwritable directory rejected");destinationUnchanged();}
#endif
    { QSettings last;last.setValue("after-rejection","still-isolated");last.sync();
      require(last.status()==QSettings::NoError,"write stays in selected profile after rejection");
    }
    for(qsizetype i=0;i<normalFiles.size();++i)require(digest(normalFiles[i])==hashes[i],"normal user/system files byte-identical");
    require(bytes(invalidFile)=="keep","invalid ordinary file unchanged");
    require(QCoreApplication::organizationName()=="Kadro" && QCoreApplication::applicationName()=="Kadro","application identity unchanged");
    std::cout<<checks<<" settings isolation assertions passed (QtCore only; no native preferences).\n";
    return 0;
  } catch(const std::exception& error) {
    std::cerr<<"Settings isolation failure after "<<checks<<" assertions: "<<error.what()<<'\n';return 1;
  }
}
