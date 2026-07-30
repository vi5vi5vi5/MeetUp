// WinRT-заголовки идут ПЕРВЫМИ, до Qt: Qt определяет макросы signals/slots/emit,
// и порядок наоборот иногда ломает генерируемый cppwinrt код.
//
// cppwinrt под /std:c++17 подключает <experimental/coroutine>, а MSVC на него
// теперь ругается ошибкой. Корутин мы здесь не используем ни одной (пул кадров
// свободнопоточный, всё на событиях), поэтому просто глушим — переводить весь
// проект на C++20 ради заголовка, который нам не нужен, смысла нет.
#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include "ScreenCapturer.h"

#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <QMutex>
#include <QVideoFrameFormat>
#include <atomic>

namespace WGC = winrt::Windows::Graphics::Capture;
namespace WGDX = winrt::Windows::Graphics::DirectX;

// ---------------------------------------------------------------------------

// Достать COM-интерфейс из объекта WinRT (текстуру из поверхности кадра).
template <typename T>
static winrt::com_ptr<T> dxgiFrom(const winrt::Windows::Foundation::IInspectable& obj) {
    auto access = obj.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<T> out;
    if (!access || FAILED(access->GetInterface(winrt::guid_of<T>(), out.put_void())))
        return nullptr;
    return out;
}

struct ScreenCapturer::Impl {
    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> ctx;
    WGDX::Direct3D11::IDirect3DDevice rtDevice{ nullptr };

    WGC::GraphicsCaptureItem item{ nullptr };
    WGC::Direct3D11CaptureFramePool pool{ nullptr };
    WGC::GraphicsCaptureSession session{ nullptr };
    winrt::event_token frameToken{};
    winrt::event_token closedToken{};

    winrt::com_ptr<ID3D11Texture2D> staging;
    QSize stagingSize;
    QSize poolSize;

    HWND hwnd = nullptr;                 // непусто только при захвате окна
    std::atomic<bool> suspended{ false };
    std::atomic<bool> closed{ false };
    // Держим на время выдачи кадра и на время разбора: кадр приезжает с потока
    // пула WGC, а stop() зовут с нашего — без замка разбор мог бы освободить
    // устройство под работающим CopyResource.
    QMutex lock;
};

// ---------------------------------------------------------------------------

ScreenCapturer::ScreenCapturer(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>()) {
    // WinRT требует инициализированного COM на потоке, где создают её объекты,
    // и MTA — потому что пул кадров свободнопоточный. На QThread апартамент уже
    // MTA (проверено пробой), так что это страховка на случай другого вызова.
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error& e) {
        if (e.code() != RPC_E_CHANGED_MODE)
            qWarning() << "ScreenCapturer: init_apartment:" << QString::fromWCharArray(e.message().c_str());
        else
            qWarning() << "ScreenCapturer: поток в STA — WGC требует MTA";
    }

    // Сторож свёрнутого окна. WGC в этом случае просто перестаёт отдавать кадры,
    // и без сторожа это не отличить от «на экране ничего не происходит»: сцена у
    // зрителя тихо застыла бы на последнем кадре, а причины не видно.
    m_watchdog = new QTimer(this);
    m_watchdog->setInterval(500);
    connect(m_watchdog, &QTimer::timeout, this, &ScreenCapturer::onWatchdog);
}

ScreenCapturer::~ScreenCapturer() {
    stop();
}

bool ScreenCapturer::isSupported() {
    // На системе без нужного контракта бросает, а не возвращает false.
    try {
        return WGC::GraphicsCaptureSession::IsSupported();
    } catch (...) {
        return false;
    }
}

bool ScreenCapturer::isRunning() const {
    return d->session != nullptr;
}

// ---------------------------------------------------------------------------

static bool ensureDevice(ScreenCapturer::Impl* d) {
    if (d->rtDevice) return true;
    // BGRA_SUPPORT обязателен: кадр WGC приходит именно в BGRA.
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    for (D3D_DRIVER_TYPE type : { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP }) {
        d->device = nullptr;
        d->ctx = nullptr;
        if (SUCCEEDED(D3D11CreateDevice(nullptr, type, nullptr, flags, nullptr, 0,
                                        D3D11_SDK_VERSION, d->device.put(), nullptr,
                                        d->ctx.put())))
            break;
    }
    if (!d->device) return false;

    auto dxgi = d->device.try_as<IDXGIDevice>();
    if (!dxgi) return false;
    winrt::com_ptr<::IInspectable> insp;
    if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), insp.put())))
        return false;
    d->rtDevice = insp.as<WGDX::Direct3D11::IDirect3DDevice>();
    return d->rtDevice != nullptr;
}

// Кадр из пула -> QVideoFrame. Исполняется на потоке пула WGC.
static void deliverFrame(ScreenCapturer* self, ScreenCapturer::Impl* d,
                         const WGC::Direct3D11CaptureFramePool& pool) {
    QMutexLocker guard(&d->lock);
    if (!d->pool || d->closed.load()) return;      // уже разбираемся

    auto frame = pool.TryGetNextFrame();
    if (!frame) return;

    const auto cs = frame.ContentSize();
    const QSize size(cs.Width, cs.Height);
    if (size.width() < 2 || size.height() < 2) return;

    auto src = dxgiFrom<ID3D11Texture2D>(frame.Surface());
    if (!src) return;

    // Промежуточная текстура для чтения процессором. Пересоздаём только при
    // смене размера — размер меняется, когда окно тянут за край.
    if (!d->staging || d->stagingSize != size) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(size.width());
        td.Height = UINT(size.height());
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        d->staging = nullptr;
        if (FAILED(d->device->CreateTexture2D(&td, nullptr, d->staging.put()))) return;
        d->stagingSize = size;
    }

    // Копируем именно область содержимого: текстура пула бывает крупнее кадра
    // (пул не пересоздают на каждый пиксель изменения размера).
    D3D11_BOX box{ 0, 0, 0, UINT(size.width()), UINT(size.height()), 1 };
    d->ctx->CopySubresourceRegion(d->staging.get(), 0, 0, 0, 0, src.get(), 0, &box);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(d->ctx->Map(d->staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) return;

    QVideoFrame vf(QVideoFrameFormat(size, QVideoFrameFormat::Format_BGRA8888));
    if (vf.map(QVideoFrame::WriteOnly)) {
        uchar* dst = vf.bits(0);
        const int dstStride = vf.bytesPerLine(0);
        const int rowBytes = qMin<int>(dstStride, int(mapped.RowPitch));
        for (int y = 0; y < size.height(); ++y)
            memcpy(dst + qsizetype(y) * dstStride,
                   static_cast<const uchar*>(mapped.pData) + qsizetype(y) * mapped.RowPitch,
                   size_t(rowBytes));
        vf.unmap();
    }
    d->ctx->Unmap(d->staging.get(), 0);

    // Размер содержимого разошёлся с пулом — пересобрать пул, иначе следующие
    // кадры будут приезжать в старом размере.
    if (size != d->poolSize) {
        d->poolSize = size;
        pool.Recreate(d->rtDevice, WGDX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
                      { size.width(), size.height() });
    }

    if (d->suspended.exchange(false))
        emit self->suspendedChanged(false);
    if (vf.isValid())
        emit self->frameReady(vf);
}

// ---------------------------------------------------------------------------

bool ScreenCapturer::startItem(bool drawCursor, const QString& what) {
    if (!d->item) {
        emit failed(QStringLiteral("Не удалось начать захват: %1.").arg(what));
        return false;
    }

    const auto sz = d->item.Size();
    d->poolSize = QSize(sz.Width, sz.Height);
    if (d->poolSize.width() < 2 || d->poolSize.height() < 2) {
        emit failed(QStringLiteral("Источник вернул пустой размер: %1.").arg(what));
        return false;
    }

    // B8G8R8A8 — сознательно, а не «что дадут».
    //
    // ЗАМЕТКА НА БУДУЩЕЕ (HDR-демонстрация для стриминга игр). На HDR-мониторе
    // WGC умеет отдавать кадр в R16G16B16A16Float с расширенным диапазоном.
    // Запросив здесь B8G8R8A8UIntNormalized, мы просим систему свести всё в SDR:
    // это правильно сейчас (кодек у нас 8-битный, приёмники ждут SDR), но именно
    // эта строка — точка входа для HDR. Полный путь потребует ещё: 10-битного
    // кодека (hevc_mf/av1_mf в сборке есть), передачи метаданных о цвете в
    // протоколе v2 и тонального отображения у приёмников без HDR.
    d->pool = WGC::Direct3D11CaptureFramePool::CreateFreeThreaded(
        d->rtDevice, WGDX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
        { d->poolSize.width(), d->poolSize.height() });
    if (!d->pool) {
        emit failed(QStringLiteral("Не удалось создать пул кадров: %1.").arg(what));
        return false;
    }

    d->frameToken = d->pool.FrameArrived(
        [this](const WGC::Direct3D11CaptureFramePool& sender,
               const winrt::Windows::Foundation::IInspectable&) {
            deliverFrame(this, d.get(), sender);
        });
    d->closedToken = d->item.Closed(
        [this](const WGC::GraphicsCaptureItem&, const winrt::Windows::Foundation::IInspectable&) {
            if (d->closed.exchange(true)) return;
            emit failed(QStringLiteral("Источник демонстрации закрыт."));
        });

    d->session = d->pool.CreateCaptureSession(d->item);
    if (!d->session) {
        emit failed(QStringLiteral("Не удалось создать сессию захвата: %1.").arg(what));
        return false;
    }

    setCursorEnabled(drawCursor);
    // Жёлтая рамка вокруг захваченного источника: на Win11 её можно убрать.
    // На более старых системах свойства нет — поэтому в try.
    try { d->session.IsBorderRequired(false); } catch (...) {}

    d->session.StartCapture();
    if (d->hwnd) m_watchdog->start();
    return true;
}

bool ScreenCapturer::startMonitor(void* hmonitor, bool drawCursor) {
    stop();
    if (!hmonitor) { emit failed(QStringLiteral("Монитор не определён.")); return false; }
    if (!ensureDevice(d.get())) {
        emit failed(QStringLiteral("Не удалось создать устройство Direct3D."));
        return false;
    }
    d->closed = false;
    try {
        auto interop = winrt::get_activation_factory<WGC::GraphicsCaptureItem,
                                                     IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForMonitor(
            static_cast<HMONITOR>(hmonitor),
            winrt::guid_of<WGC::GraphicsCaptureItem>(),
            winrt::put_abi(d->item)));
    } catch (const winrt::hresult_error& e) {
        emit failed(QStringLiteral("Захват экрана недоступен: %1")
                        .arg(QString::fromWCharArray(e.message().c_str())));
        return false;
    }
    return startItem(drawCursor, QStringLiteral("экран"));
}

bool ScreenCapturer::startWindow(void* hwnd, bool drawCursor) {
    stop();
    HWND h = static_cast<HWND>(hwnd);
    if (!h || !IsWindow(h)) { emit failed(QStringLiteral("Окно недоступно.")); return false; }
    if (!ensureDevice(d.get())) {
        emit failed(QStringLiteral("Не удалось создать устройство Direct3D."));
        return false;
    }
    d->closed = false;
    d->hwnd = h;
    try {
        auto interop = winrt::get_activation_factory<WGC::GraphicsCaptureItem,
                                                     IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForWindow(
            h, winrt::guid_of<WGC::GraphicsCaptureItem>(), winrt::put_abi(d->item)));
    } catch (const winrt::hresult_error& e) {
        d->hwnd = nullptr;
        emit failed(QStringLiteral("Захват окна недоступен: %1")
                        .arg(QString::fromWCharArray(e.message().c_str())));
        return false;
    }
    return startItem(drawCursor, QStringLiteral("окно"));
}

void ScreenCapturer::setCursorEnabled(bool on) {
    if (!d->session) return;
    // Курсор рисует система — это и есть причина, по которой своя дорисовка
    // (ScreenCursor + blendCursor) больше не нужна. Свойство появилось в
    // Windows 10 2004; на более старом просто останется как было.
    try { d->session.IsCursorCaptureEnabled(on); } catch (...) {}
}

void ScreenCapturer::stop() {
    if (m_watchdog) m_watchdog->stop();

    // Сначала снимаем подписку и только потом закрываем: иначе колбэк может
    // приехать уже на разобранное. Замок — против колбэка, который в этот
    // момент уже внутри deliverFrame.
    {
        QMutexLocker guard(&d->lock);
        if (d->pool && d->frameToken) d->pool.FrameArrived(d->frameToken);
        if (d->item && d->closedToken) d->item.Closed(d->closedToken);
        d->frameToken = {};
        d->closedToken = {};

        if (d->session) { d->session.Close(); d->session = nullptr; }
        if (d->pool) { d->pool.Close(); d->pool = nullptr; }
        d->item = nullptr;
        d->staging = nullptr;
        d->stagingSize = QSize();
        d->poolSize = QSize();
        d->hwnd = nullptr;
        d->suspended = false;
    }
}

// Сторож работает только для окна: у монитора отсутствие кадров означает всего
// лишь неподвижный экран, а вот свёрнутое окно WGC не отдаёт вовсе.
void ScreenCapturer::onWatchdog() {
    HWND h = d->hwnd;
    if (!h) return;

    if (!IsWindow(h)) {
        m_watchdog->stop();
        if (!d->closed.exchange(true))
            emit failed(QStringLiteral("Окно больше недоступно."));
        return;
    }

    const bool iconic = IsIconic(h);
    if (iconic && !d->suspended.exchange(true))
        emit suspendedChanged(true);
    else if (!iconic && d->suspended.exchange(false))
        emit suspendedChanged(false);
}
