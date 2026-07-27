#include "ScreenAudioCapture.h"
#include <QDateTime>
#include <QDebug>

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <wrl/implements.h>
#include <cmath>

// ВНИМАНИЕ: <initguid.h> сюда включать нельзя — с ним cguid.h ломается на
// __uuidof (проверено, C2059). Все нужные GUID берутся из __declspec(uuid).

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::FtmBase;

static const int kRate = 48000;
static const int kChannels = 2;        // просим стерео, отдаём моно
static const int kFrameSamples = 960;  // 20 мс при 48 кГц

// ActivateAudioInterfaceAsync отвечает не сразу — ждём завершения на событии.
namespace {
class ActivateHandler
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase,
                          IActivateAudioInterfaceCompletionHandler> {
public:
    HANDLE done = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    HRESULT hr = E_FAIL;
    ComPtr<IAudioClient> client;

    ~ActivateHandler() override { if (done) CloseHandle(done); }

    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        HRESULT activateHr = E_FAIL;
        ComPtr<IUnknown> unknown;
        hr = op->GetActivateResult(&activateHr, &unknown);
        if (SUCCEEDED(hr)) hr = activateHr;
        if (SUCCEEDED(hr)) hr = unknown.As(&client);
        SetEvent(done);
        return S_OK;
    }
};
} // namespace

ScreenAudioCapture::ScreenAudioCapture(QObject* parent) : QObject(parent) {}

ScreenAudioCapture::~ScreenAudioCapture() { stop(); }

void ScreenAudioCapture::start() {
    if (m_running.load()) return;
    // Прошлый поток мог завершиться сам (не поднялся захват) — тогда объект
    // потока всё ещё joinable, и присваивание в него означало бы std::terminate.
    if (m_thread.joinable()) m_thread.join();
    m_stop = false;
    m_running = true;
    m_thread = std::thread([this] { run(); });
}

void ScreenAudioCapture::stop() {
    if (!m_thread.joinable()) { m_running = false; return; }
    m_stop = true;
    m_thread.join();
    m_running = false;
}

void ScreenAudioCapture::run() {
    // Свой поток и своя MTA-квартира: ActivateAudioInterfaceAsync в STA
    // GUI-потока ведёт себя непредсказуемо, а захват всё равно должен жить
    // отдельно — он спит на событии устройства.
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool coOk = SUCCEEDED(coHr);

    auto fail = [this](const QString& text) {
        m_running = false;
        emit failed(text);
    };

    if (!coOk) {
        fail(QStringLiteral("Не удалось инициализировать COM для захвата звука."));
        return;
    }

    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
    // Всё, кроме нас: иначе собеседники услышали бы сами себя эхом.
    params.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT activateParams{};
    activateParams.vt = VT_BLOB;
    activateParams.blob.cbSize = sizeof(params);
    activateParams.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    auto handler = Make<ActivateHandler>();
    ComPtr<IActivateAudioInterfaceAsyncOperation> op;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                             __uuidof(IAudioClient), &activateParams,
                                             handler.Get(), &op);
    if (FAILED(hr)) {
        CoUninitialize();
        fail(QStringLiteral("Windows не дала захватить звук приложений (0x%1).")
                 .arg(quint32(hr), 8, 16, QChar('0')));
        return;
    }
    WaitForSingleObject(handler->done, 5000);
    if (FAILED(handler->hr) || !handler->client) {
        CoUninitialize();
        fail(QStringLiteral("Захват звука приложений недоступен (0x%1). "
                            "Нужна Windows 10 версии 2004 или новее.")
                 .arg(quint32(handler->hr), 8, 16, QChar('0')));
        return;
    }

    // У process loopback нет GetMixFormat — формат задаём мы, и это удобно:
    // просим сразу то, что нужно кодеку, и ресемплировать ничего не надо.
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = kChannels;
    fmt.nSamplesPerSec = kRate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = WORD(fmt.nChannels * fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    ComPtr<IAudioClient> client = handler->client;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                            20 * 10000 /* 20 мс в 100-нс единицах */, 0, &fmt, nullptr);
    if (FAILED(hr)) {
        CoUninitialize();
        fail(QStringLiteral("Звуковая подсистема отказала при настройке (0x%1).")
                 .arg(quint32(hr), 8, 16, QChar('0')));
        return;
    }

    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    ComPtr<IAudioCaptureClient> capture;
    if (FAILED(client->SetEventHandle(evt))
        || FAILED(client->GetService(__uuidof(IAudioCaptureClient), &capture))
        || FAILED(client->Start())) {
        if (evt) CloseHandle(evt);
        CoUninitialize();
        fail(QStringLiteral("Не удалось запустить захват звука приложений."));
        return;
    }

    qInfo() << "ScreenAudioCapture: пошёл, 48000 Гц, стерео -> моно";

    QByteArray mono;                     // накопитель моно-сэмплов до целого кадра
    mono.reserve(kFrameSamples * 2 * 4);

    while (!m_stop.load()) {
        if (WaitForSingleObject(evt, 100) != WAIT_OBJECT_0) continue;

        UINT32 packet = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                break;

            const int oldSize = mono.size();
            mono.resize(oldSize + int(frames) * 2);
            qint16* dst = reinterpret_cast<qint16*>(mono.data() + oldSize);

            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data) {
                memset(dst, 0, size_t(frames) * 2);
            } else {
                // Стерео -> моно полусуммой: панорама в демонстрации не нужна,
                // а моно вдвое дешевле и в кодеке, и в сети.
                const qint16* src = reinterpret_cast<const qint16*>(data);
                for (UINT32 i = 0; i < frames; ++i)
                    dst[i] = qint16((qint32(src[i * 2]) + src[i * 2 + 1]) / 2);
            }
            capture->ReleaseBuffer(frames);
        }

        // Режем накопленное ровно по кадрам 20 мс.
        const int frameBytes = kFrameSamples * 2;
        while (mono.size() >= frameBytes) {
            emit pcmFrame(mono.left(frameBytes), QDateTime::currentMSecsSinceEpoch());
            mono.remove(0, frameBytes);
        }
    }

    client->Stop();
    CloseHandle(evt);
    capture.Reset();
    client.Reset();
    CoUninitialize();
    m_running = false;
}
