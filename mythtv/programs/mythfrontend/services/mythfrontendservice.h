#ifndef MYTHFRONTENDSERVICE_H
#define MYTHFRONTENDSERVICE_H

// MythTV
#include "libmythbase/http/mythhttpservice.h"

#define FRONTEND_SERVICE QString("/Frontend/")
#define FRONTEND_HANDLE  QString("Frontend")

class FrontendStatus : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("Version",        "1.1")
    Q_CLASSINFO("State",          "type=QString")
    Q_CLASSINFO("ChapterTimes",   "type=QString;name=Chapter")
    Q_CLASSINFO("SubtitleTracks", "type=QString;name=Track")
    Q_CLASSINFO("AudioTracks",    "type=QString;name=Track")
    SERVICE_PROPERTY(QString,      Name,           name)
    SERVICE_PROPERTY(QString,      Version,        version)
    SERVICE_PROPERTY(QVariantMap,  State,          state)
    SERVICE_PROPERTY(QVariantList, ChapterTimes,   chapterTimes)
    SERVICE_PROPERTY(QVariantMap,  SubtitleTracks, subtitleTracks)
    SERVICE_PROPERTY(QVariantMap,  AudioTracks,    audioTracks)

  public:
    FrontendStatus(const QString& Name, const QString& Version, const QVariantMap& State);
};

Q_DECLARE_METATYPE(FrontendStatus*)

class FrontendActionList : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("Version",    "1.0")
    Q_CLASSINFO("ActionList", "type=QString;name=Action")
    SERVICE_PROPERTY(QVariantMap, ActionList, actionList)

  public:
    FrontendActionList(const QVariantMap& List);
};

Q_DECLARE_METATYPE(FrontendActionList*)

class MythFrontendService : public MythHTTPService
{
    Q_OBJECT
    Q_CLASSINFO("Version",          "1.0")
    Q_CLASSINFO("SendAction",       "methods=POST")
    Q_CLASSINFO("SendKey",          "methods=POST")
    Q_CLASSINFO("PlayVideo",        "methods=POST")
    Q_CLASSINFO("PlayRecording",    "methods=POST")
    Q_CLASSINFO("SendMessage",      "methods=POST")
    Q_CLASSINFO("SendNotification", "methods=POST")
    Q_CLASSINFO("GetContextList",   "name=StringList") // Consistency with old code

  public slots:
    bool        SendAction      (const QString& Action, const QString& Value, uint Width, uint Height);
    bool        SendKey         (const QString& Key);
    FrontendActionList* GetActionList(const QString& Context);
    QStringList GetContextList  ();
    FrontendStatus* GetStatus   ();
    bool        PlayVideo       (const QString& Id, bool UseBookmark);
    bool        PlayRecording   (int RecordedId, int ChanId, const QDateTime& StartTime);
    bool        SendMessage     (const QString& Message, uint Timeout);
    bool        SendNotification(bool  Error,                const QString& Type,
                                 const QString& Message,     const QString& Origin,
                                 const QString& Description, const QString& Image,
                                 const QString& Extra,       const QString& ProgressText,
                                 float Progress,             std::chrono::seconds Timeout,
                                 bool  Fullscreen,           uint  Visibility,
                                 uint  Priority);

  public:
    MythFrontendService();
   ~MythFrontendService() override = default;
    static void RegisterCustomTypes();

  protected:
    static bool IsValidAction(const QString& Action);

  private:
    Q_DISABLE_COPY(MythFrontendService)
};

#endif
