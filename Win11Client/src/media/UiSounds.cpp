#include "UiSounds.h"
#include "AudioEngine.h"
#include "../MediaSettings.h"

#include <QAudioDevice>
#include <QDateTime>
#include <QDebug>
#include <QSoundEffect>
#include <QUrl>

namespace {

// Каталог звуков: файл, «уведомление ли это» и наименьший промежуток между
// двумя одинаковыми звуками.
//
// Деление на уведомления и действия — не украшение. Уведомления (сообщение,
// приход, уход) молчат, когда снят звук конференции: человек попросил тишины,
// и чужая активность к нему сейчас не относится. Собственные действия звучат
// всегда — иначе кнопка микрофона перестаёт отзываться ровно тогда, когда
// обратная связь нужнее всего, ведь разговора уже не слышно.
//
// Промежутки прикрывают пачки: пять человек, вошедших разом после падения
// сервера, должны дать один звук, а не пять. Тумблерам хватает 60 мс — это
// защита от дребезга горячей клавиши, а не от потока событий.
struct Spec { const char* name; bool notification; int minGapMs; };

constexpr Spec kCatalogue[] = {
    { "toggle-on",  false,   60 },
    { "toggle-off", false,   60 },
    { "deafen-on",  false,   60 },
    { "deafen-off", false,   60 },
    { "share-on",   false,  200 },
    { "share-off",  false,  200 },
    { "room-join",  false, 1000 },
    { "message",    true,   250 },
    { "peer-join",  true,   400 },
    { "peer-leave", true,   400 },
};

const Spec* findSpec(const QString& name) {
    for (const Spec& s : kCatalogue)
        if (name == QLatin1String(s.name)) return &s;
    return nullptr;
}

} // namespace

UiSounds::UiSounds(MediaSettings* av, AudioEngine* audio, QObject* parent)
    : QObject(parent), m_av(av), m_audio(audio) {
    build();
    // Сменили наушники в настройках — звуки уходят следом. Пул проще собрать
    // заново, чем переставлять устройство у десяти уже загруженных эффектов.
    connect(av, &MediaSettings::outIdChanged, this, &UiSounds::build);
}

void UiSounds::build() {
    for (QSoundEffect* fx : std::as_const(m_fx))
        fx->deleteLater();   // не delete: звук мог ещё играть
    m_fx.clear();

    const QAudioDevice out = m_av->audioOutput();
    for (const Spec& s : kCatalogue) {
        const QString name = QString::fromLatin1(s.name);
        auto* fx = new QSoundEffect(out, this);
        // Загрузка асинхронная, поэтому пул строится на старте, а не в комнате:
        // к первому нажатию кнопки файлы уже в памяти и звук не опаздывает.
        fx->setSource(QUrl(QStringLiteral(
            "qrc:/qt/qml/MeetUp/resources/sounds/%1.wav").arg(name)));
        // Уровни сведены в самих файлах (сообщение громче всех, уход тише
        // всех), так что здесь ручку трогать нечем.
        fx->setVolume(1.0f);
        connect(fx, &QSoundEffect::statusChanged, this, [fx, name] {
            if (fx->status() == QSoundEffect::Error)
                qWarning() << "UiSounds: не загрузился звук" << name << fx->source();
        });
        m_fx.insert(name, fx);
    }
}

void UiSounds::play(const QString& name) {
    if (!m_av->uiSounds()) return;

    const Spec* spec = findSpec(name);
    if (!spec) return;
    if (spec->notification && m_audio->outputMuted()) return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64& last = m_lastAt[name];
    if (last != 0 && now - last < spec->minGapMs) return;
    last = now;

    QSoundEffect* fx = m_fx.value(name);
    if (!fx || fx->status() == QSoundEffect::Error) return;
    fx->play();
}
