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
#include <chrono>

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

    // ---- путь через видеокарту (см. setTargetBox) ----
    winrt::com_ptr<ID3D11VideoDevice> vdev;
    winrt::com_ptr<ID3D11VideoContext> vctx;
    winrt::com_ptr<ID3D11VideoProcessorEnumerator> vpEnum;
    winrt::com_ptr<ID3D11VideoProcessor> vp;
    winrt::com_ptr<ID3D11Texture2D> nv12;              // выход процессора
    winrt::com_ptr<ID3D11VideoProcessorOutputView> nv12View;
    winrt::com_ptr<ID3D11Texture2D> nv12Staging;       // он же для чтения ЦП
    // Кадры приходят из пула WGC по кругу (их там два), а представление входа
    // привязано к конкретной текстуре — держим готовые, чтобы не создавать их
    // заново на каждый кадр.
    QHash<void*, winrt::com_ptr<ID3D11VideoProcessorInputView>> inViews;
    QSize vpSrc, vpDst;
    QSize targetBox;                                   // пусто — не уменьшать
    // Гасится при первой же неудаче: остаться без демонстрации хуже, чем
    // отдать её медленным путём через процессор.
    bool gpuPath = true;

    HWND hwnd = nullptr;                 // непусто только при захвате окна
    std::atomic<bool> suspended{ false };
    std::atomic<bool> closed{ false };
    std::atomic<qint64> copyUs{ 0 };     // см. lastCopyMicros()
    // Держим на время выдачи кадра и на время разбора: кадр приезжает с потока
    // пула WGC, а stop() зовут с нашего — без замка разбор мог бы освободить
    // устройство под работающим CopyResource.
    QMutex lock;
};

// ---------------------------------------------------------------------------

ScreenCapturer::ScreenCapturer(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>()) {}

// Всё, что принадлежит ПОТОКУ, а не объекту. Раньше это стояло в конструкторе
// и работало по случайности: объект создавали прямо на нужном потоке. Теперь
// он создаётся на GUI-потоке и переезжает, а апартамент переехать не может —
// он свойство потока, и заявлять его надо оттуда, где потом пойдут вызовы.
void ScreenCapturer::init() {
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
    if (m_watchdog) return;
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

qint64 ScreenCapturer::lastCopyMicros() const {
    return d->copyUs.load(std::memory_order_relaxed);
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

// Целевой размер: вписать исходный кадр в рамку, сохранив пропорции. Чётный —
// того же требуют и кодеки (§5.5), и формат NV12 с его половинной цветностью.
static QSize fitInto(const QSize& src, const QSize& box) {
    if (box.isEmpty() || src.isEmpty()) return src;
    const double k = qMin(1.0, qMin(double(box.width()) / src.width(),
                                    double(box.height()) / src.height()));
    return QSize(qMax(2, int(src.width() * k) & ~1),
                 qMax(2, int(src.height() * k) & ~1));
}

// Собрать (или пересобрать) процессор видеокарты под пару размеров.
// Возвращает false, если чего-то не хватает — тогда работаем по-старому.
static bool ensureProcessor(ScreenCapturer::Impl* d, const QSize& src, const QSize& dst) {
    if (d->vp && d->vpSrc == src && d->vpDst == dst) return true;

    d->inViews.clear();
    d->nv12View = nullptr;
    d->nv12 = nullptr;
    d->nv12Staging = nullptr;
    d->vp = nullptr;
    d->vpEnum = nullptr;

    if (!d->vdev) d->vdev = d->device.try_as<ID3D11VideoDevice>();
    if (!d->vctx) d->vctx = d->ctx.try_as<ID3D11VideoContext>();
    if (!d->vdev || !d->vctx) return false;

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd{};
    cd.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    cd.InputWidth = UINT(src.width());
    cd.InputHeight = UINT(src.height());
    cd.OutputWidth = UINT(dst.width());
    cd.OutputHeight = UINT(dst.height());
    cd.InputFrameRate = { 60, 1 };
    cd.OutputFrameRate = { 60, 1 };
    cd.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    if (FAILED(d->vdev->CreateVideoProcessorEnumerator(&cd, d->vpEnum.put()))) return false;
    if (FAILED(d->vdev->CreateVideoProcessor(d->vpEnum.get(), 0, d->vp.put()))) return false;

    // Выход процессора: NV12 нужного размера. BIND_RENDER_TARGET обязателен —
    // без него не создать представление выхода.
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(dst.width());
    td.Height = UINT(dst.height());
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_NV12;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(d->device->CreateTexture2D(&td, nullptr, d->nv12.put()))) return false;

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ovd{};
    ovd.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    if (FAILED(d->vdev->CreateVideoProcessorOutputView(d->nv12.get(), d->vpEnum.get(),
                                                       &ovd, d->nv12View.put())))
        return false;

    td.BindFlags = 0;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(d->device->CreateTexture2D(&td, nullptr, d->nv12Staging.put()))) return false;

    // Цветовые пространства задаём явно, чтобы картинка не поехала по сравнению
    // с прежним путём: на входе RGB полного диапазона, на выходе BT.601 с
    // урезанным (16–235) — ровно то, что по умолчанию делал sws_scale.
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE inCs{};
    inCs.RGB_Range = 0;                       // 0-255
    d->vctx->VideoProcessorSetStreamColorSpace(d->vp.get(), 0, &inCs);
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE outCs{};
    outCs.YCbCr_Matrix = 0;                   // BT.601
    outCs.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
    d->vctx->VideoProcessorSetOutputColorSpace(d->vp.get(), &outCs);

    const RECT srcRect{ 0, 0, src.width(), src.height() };
    const RECT dstRect{ 0, 0, dst.width(), dst.height() };
    d->vctx->VideoProcessorSetStreamSourceRect(d->vp.get(), 0, TRUE, &srcRect);
    d->vctx->VideoProcessorSetStreamDestRect(d->vp.get(), 0, TRUE, &dstRect);
    d->vctx->VideoProcessorSetOutputTargetRect(d->vp.get(), TRUE, &dstRect);

    d->vpSrc = src;
    d->vpDst = dst;
    qInfo() << "ScreenCapturer: видеокарта готовит кадр" << src.width() << "x" << src.height()
            << "->" << dst.width() << "x" << dst.height() << "NV12";
    return true;
}

// Кадр -> NV12 нужного размера силами видеокарты, затем чтение маленького
// результата. false — не получилось, зовущий уходит на прежний путь.
static bool gpuConvert(ScreenCapturer::Impl* d, ID3D11Texture2D* src,
                       const QSize& srcSize, const QSize& dstSize, QVideoFrame* out) {
    if (!ensureProcessor(d, srcSize, dstSize)) return false;

    // Представление входа кэшируем по адресу текстуры: пул WGC гоняет по кругу
    // два буфера, и пересоздавать их каждый кадр незачем.
    winrt::com_ptr<ID3D11VideoProcessorInputView>& iv = d->inViews[src];
    if (!iv) {
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC ivd{};
        ivd.FourCC = 0;
        ivd.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        ivd.Texture2D.MipSlice = 0;
        ivd.Texture2D.ArraySlice = 0;
        const HRESULT hr = d->vdev->CreateVideoProcessorInputView(src, d->vpEnum.get(),
                                                                  &ivd, iv.put());
        if (FAILED(hr)) {
            d->inViews.remove(src);
            qWarning("ScreenCapturer: CreateVideoProcessorInputView = 0x%08lX",
                     static_cast<unsigned long>(hr));
            return false;
        }
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = iv.get();
    const HRESULT hrBlt = d->vctx->VideoProcessorBlt(d->vp.get(), d->nv12View.get(), 0, 1, &stream);
    if (FAILED(hrBlt)) {
        qWarning("ScreenCapturer: VideoProcessorBlt = 0x%08lX",
                 static_cast<unsigned long>(hrBlt));
        return false;
    }

    d->ctx->CopyResource(d->nv12Staging.get(), d->nv12.get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT hrMap = d->ctx->Map(d->nv12Staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hrMap)) {
        qWarning("ScreenCapturer: Map(NV12) = 0x%08lX", static_cast<unsigned long>(hrMap));
        return false;
    }

    // Раскладка NV12 в отображённой текстуре: плоскость яркости высотой H, за
    // ней цветность высотой H/2, обе с одинаковым шагом строки. Это контракт
    // D3D11, на него и опираемся.
    //
    // А вот DepthPitch проверять как «полтора кадра» НЕЛЬЗЯ, хотя и хочется:
    // драйвер AMD сообщает здесь 8294400 при RowPitch 3840 и высоте 2160, то
    // есть ровно размер ОДНОЙ плоскости яркости. Цветность при этом лежит сразу
    // за ней, как и положено. Строгая проверка забраковала бы совершенно
    // правильный кадр и увела весь захват на медленный путь — что она и делала.
    const int h = dstSize.height();
    const bool layoutOk = mapped.pData && mapped.RowPitch > 0
        && (mapped.DepthPitch == 0 || mapped.DepthPitch >= mapped.RowPitch * UINT(h));
    if (!layoutOk)
        qWarning("ScreenCapturer: раскладка NV12 не та: pData=%p RowPitch=%u DepthPitch=%u h=%d",
                 mapped.pData, mapped.RowPitch, mapped.DepthPitch, h);
    QVideoFrame vf;
    if (layoutOk) {
        vf = QVideoFrame(QVideoFrameFormat(dstSize, QVideoFrameFormat::Format_NV12));
        if (!vf.map(QVideoFrame::WriteOnly))
            qWarning("ScreenCapturer: QVideoFrame(NV12) не отображается на запись");
        else if (vf.planeCount() < 2)
            qWarning("ScreenCapturer: QVideoFrame(NV12) отдал %d плоскостей", vf.planeCount());
        if (vf.isMapped() && vf.planeCount() >= 2) {
            const uchar* srcY = static_cast<const uchar*>(mapped.pData);
            const uchar* srcUV = srcY + qsizetype(mapped.RowPitch) * h;
            for (int y = 0; y < h; ++y)
                memcpy(vf.bits(0) + qsizetype(y) * vf.bytesPerLine(0),
                       srcY + qsizetype(y) * mapped.RowPitch,
                       size_t(qMin(vf.bytesPerLine(0), int(mapped.RowPitch))));
            for (int y = 0; y < h / 2; ++y)
                memcpy(vf.bits(1) + qsizetype(y) * vf.bytesPerLine(1),
                       srcUV + qsizetype(y) * mapped.RowPitch,
                       size_t(qMin(vf.bytesPerLine(1), int(mapped.RowPitch))));
            vf.unmap();
        } else {
            vf = QVideoFrame();
        }
    }
    d->ctx->Unmap(d->nv12Staging.get(), 0);

    if (!vf.isValid()) return false;
    *out = vf;
    return true;
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

    // Основной путь: уменьшение и перевод в NV12 делает видеокарта, а обратно
    // читается уже маленький кадр. На 4К это разница между 33 МБ и 3 МБ на
    // кадр — и между 29 мс и парой миллисекунд.
    if (d->gpuPath) {
        const QSize dst = fitInto(size, d->targetBox);
        const auto copyStart = std::chrono::steady_clock::now();
        QVideoFrame vf;
        if (gpuConvert(d, src.get(), size, dst, &vf)) {
            d->copyUs.store(std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - copyStart).count(),
                            std::memory_order_relaxed);
            if (size != d->poolSize) {
                d->poolSize = size;
                d->inViews.clear();          // пул пересоберёт свои текстуры
                pool.Recreate(d->rtDevice, WGDX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
                              { size.width(), size.height() });
            }
            if (d->suspended.exchange(false))
                emit self->suspendedChanged(false);
            emit self->frameReady(vf);
            return;
        }
        // Не сложилось — дальше по-старому, и больше не пробуем.
        qWarning() << "ScreenCapturer: видеокарта не справилась, переходим на путь через ЦП";
        d->gpuPath = false;
    }

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
    const auto copyStart = std::chrono::steady_clock::now();
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
    d->copyUs.store(std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - copyStart).count(),
                    std::memory_order_relaxed);

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
    if (d->hwnd && m_watchdog) m_watchdog->start();
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

// Кадр окна приходит чуть КРУПНЕЕ самого окна: у окна 3840x2160 замер дал
// 3848x2180. Это расширенные границы от DWM — тень и рамка, которые система
// считает частью окна. Обрезать не стали намеренно: лишние пиксели по краям
// зрителю не мешают (это фон окна, а не чужое содержимое), а обрезка означала бы
// ещё один проход по кадру ради косметики. Если однажды понадобится — считать
// границы через DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS) и вырезать
// в deliverFrame, там уже есть D3D11_BOX.
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

void ScreenCapturer::setTargetBox(const QSize& box) {
    QMutexLocker guard(&d->lock);
    if (d->targetBox == box) return;
    d->targetBox = box;
    d->vpSrc = QSize();                // заставит пересобрать процессор
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
        // Представления входа держат текстуры пула — отпускаем их вместе с ним.
        d->inViews.clear();
        d->vpSrc = QSize();
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
