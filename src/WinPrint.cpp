// Printing through the Windows print dialog with live preview.
//
// Classic Win32 printing (PrintDlgEx, which Qt uses) gets the Windows 11
// dialog without a preview. The preview only works when the app prints
// through Windows.Graphics.Printing, like UWP/WinUI apps do:
//   PrintManager (via IPrintManagerInterop for our HWND)
//     -> PrintTaskRequested: create a PrintTask whose source is ours
//     -> our IPrintDocumentSource gives the dialog a preview page collection:
//          Paginate: tell it the page count
//          MakePage: draw a page into a DXGI surface for the preview pane
//     -> MakeDocument (after Print): draw the pages with Direct2D into the
//        print job (ID2D1PrintControl).
// The dialog calls us on its own threads; drawing pages needs the GUI thread
// (Qt PDF, fonts), so those calls are forwarded to it.

// Windows/WinRT headers first, before Qt's.
#include <unknwn.h>
#include <windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Printing.h>

#include <DocumentSource.h>
#include <DocumentTarget.h>
#include <PrintManagerInterop.h>
#include <PrintPreview.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>

#include "WinPrint.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QPointer>
#include <QThread>
#include <QWidget>

#include <atomic>
#include <mutex>
#include <vector>

namespace wf = winrt::Windows::Foundation;
namespace wgp = winrt::Windows::Graphics::Printing;

namespace WinPrint {

namespace {

constexpr float kPreviewDpi = 110.0f; // preview pane images
constexpr float kPrintDpi = 300.0f;   // the pages sent to the printer

// Runs `fn` on the GUI thread and waits for it.
template <typename F> void onGuiThread(F &&fn)
{
    if (QThread::currentThread() == QCoreApplication::instance()->thread())
        fn();
    else
        QMetaObject::invokeMethod(QCoreApplication::instance(), fn, Qt::BlockingQueuedConnection);
}

QImage paperImage(const PageImage &render, int index, float widthDips, float heightDips, float dpi)
{
    QImage img;
    onGuiThread([&] {
        img = render(index, QSizeF(widthDips * 72.0 / 96.0, heightDips * 72.0 / 96.0), dpi)
                  .convertToFormat(QImage::Format_ARGB32_Premultiplied); // = BGRA, premultiplied
    });
    return img;
}

struct DocumentSource
    : winrt::implements<DocumentSource, wgp::IPrintDocumentSource, IPrintDocumentPageSource, IPrintPreviewPageCollection>
{
    DocumentSource(int pageCount, PageImage render) : m_count(pageCount), m_render(std::move(render)) {}

    // ---- IPrintDocumentPageSource
    HRESULT STDMETHODCALLTYPE GetPreviewPageCollection(IPrintDocumentPackageTarget *target,
                                                       IPrintPreviewPageCollection **collection) override
    {
        try {
            std::lock_guard lock(m_mutex);
            m_preview = nullptr;
            winrt::check_hresult(target->GetPackageTarget(__uuidof(IPrintPreviewDxgiPackageTarget),
                                                          __uuidof(IPrintPreviewDxgiPackageTarget),
                                                          m_preview.put_void()));
            *collection = this;
            AddRef();
            return S_OK;
        } catch (...) {
            return winrt::to_hresult();
        }
    }

    HRESULT STDMETHODCALLTYPE MakeDocument(::IInspectable *optionsAbi, IPrintDocumentPackageTarget *target) override
    {
        try {
            wf::IInspectable inspectable;
            winrt::copy_from_abi(inspectable, optionsAbi);
            const auto core = inspectable.as<wgp::IPrintTaskOptionsCore>();

            // Which pages: a custom range from the dialog, or all of them.
            std::vector<int> pages;
            try {
                const auto ranges = inspectable.as<wgp::PrintTaskOptions>().CustomPageRanges();
                for (const auto &r : ranges)
                    for (int p = r.FirstPageNumber(); p <= r.LastPageNumber(); ++p)
                        if (p >= 1 && p <= m_count)
                            pages.push_back(p - 1);
            } catch (...) {
            }
            if (pages.empty())
                for (int i = 0; i < m_count; ++i)
                    pages.push_back(i);

            ensureDevice();
            winrt::com_ptr<ID2D1DeviceContext> context;
            winrt::check_hresult(m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, context.put()));
            winrt::com_ptr<IWICImagingFactory> wic;
            winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                  IID_PPV_ARGS(wic.put())));
            D2D1_PRINT_CONTROL_PROPERTIES props = {D2D1_PRINT_FONT_SUBSET_MODE_DEFAULT, kPrintDpi,
                                                   D2D1_COLOR_SPACE_SRGB};
            winrt::com_ptr<ID2D1PrintControl> control;
            winrt::check_hresult(m_d2dDevice->CreatePrintControl(wic.get(), target, &props, control.put()));

            for (size_t i = 0; i < pages.size(); ++i) {
                const wgp::PrintPageDescription desc = core.GetPageDescription(uint32_t(i + 1));
                const float w = desc.PageSize.Width, h = desc.PageSize.Height; // DIPs
                const QImage img = paperImage(m_render, pages[i], w, h, kPrintDpi);
                if (img.isNull())
                    winrt::throw_hresult(E_FAIL);

                winrt::com_ptr<ID2D1CommandList> list;
                winrt::check_hresult(context->CreateCommandList(list.put()));
                context->SetTarget(list.get());
                context->BeginDraw();
                winrt::com_ptr<ID2D1Bitmap1> bitmap;
                const D2D1_BITMAP_PROPERTIES1 bp = {
                    {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, kPrintDpi, kPrintDpi,
                    D2D1_BITMAP_OPTIONS_NONE, nullptr};
                winrt::check_hresult(context->CreateBitmap(D2D1::SizeU(img.width(), img.height()), img.constBits(),
                                                           UINT32(img.bytesPerLine()), &bp, bitmap.put()));
                context->DrawBitmap(bitmap.get(), D2D1::RectF(0, 0, w, h));
                winrt::check_hresult(context->EndDraw());
                winrt::check_hresult(list->Close());
                winrt::check_hresult(control->AddPage(list.get(), D2D1::SizeF(w, h), nullptr));
            }
            winrt::check_hresult(control->Close());
            return S_OK;
        } catch (...) {
            return winrt::to_hresult();
        }
    }

    // ---- IPrintPreviewPageCollection
    HRESULT STDMETHODCALLTYPE Paginate(UINT32, ::IInspectable *optionsAbi) override
    {
        try {
            wf::IInspectable inspectable;
            winrt::copy_from_abi(inspectable, optionsAbi);
            const wgp::PrintPageDescription desc =
                inspectable.as<wgp::IPrintTaskOptionsCore>().GetPageDescription(1);
            std::lock_guard lock(m_mutex);
            m_paper = desc.PageSize;
            winrt::check_hresult(m_preview->SetJobPageCount(FinalPageCount, UINT32(m_count)));
            winrt::check_hresult(m_preview->InvalidatePreview());
            return S_OK;
        } catch (...) {
            return winrt::to_hresult();
        }
    }

    HRESULT STDMETHODCALLTYPE MakePage(UINT32 jobPage, FLOAT width, FLOAT height) override
    {
        try {
            if (jobPage == JOB_PAGE_APPLICATION_DEFINED)
                jobPage = 1;
            if (jobPage < 1 || int(jobPage) > m_count)
                return S_OK;
            if (width <= 0 || height <= 0) {
                std::lock_guard lock(m_mutex);
                width = m_paper.Width;
                height = m_paper.Height;
            }
            const QImage img = paperImage(m_render, int(jobPage) - 1, width, height, kPreviewDpi);
            if (img.isNull())
                return E_FAIL;

            ensureDevice();
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = UINT(img.width());
            td.Height = UINT(img.height());
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            const D3D11_SUBRESOURCE_DATA data = {img.constBits(), UINT(img.bytesPerLine()), 0};
            winrt::com_ptr<ID3D11Texture2D> texture;
            winrt::check_hresult(m_d3d->CreateTexture2D(&td, &data, texture.put()));
            const auto surface = texture.as<IDXGISurface>();

            std::lock_guard lock(m_mutex);
            winrt::check_hresult(m_preview->DrawPage(jobPage, surface.get(), kPreviewDpi, kPreviewDpi));
            return S_OK;
        } catch (...) {
            return winrt::to_hresult();
        }
    }

private:
    void ensureDevice()
    {
        std::lock_guard lock(m_deviceMutex);
        if (m_d3d)
            return;
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                       D3D11_SDK_VERSION, m_d3d.put(), nullptr, nullptr);
        if (FAILED(hr)) // no usable GPU: the software rasterizer
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                                   m_d3d.put(), nullptr, nullptr);
        winrt::check_hresult(hr);
        winrt::com_ptr<ID2D1Factory1> factory;
        D2D1_FACTORY_OPTIONS fo = {};
        winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), &fo,
                                               factory.put_void()));
        winrt::check_hresult(factory->CreateDevice(m_d3d.as<IDXGIDevice>().get(), m_d2dDevice.put()));
    }

    const int m_count;
    const PageImage m_render;
    std::mutex m_mutex, m_deviceMutex;
    winrt::com_ptr<IPrintPreviewDxgiPackageTarget> m_preview;
    wf::Size m_paper{816, 1056}; // Letter, in DIPs, until the dialog says otherwise
    winrt::com_ptr<ID3D11Device> m_d3d;
    winrt::com_ptr<ID2D1Device> m_d2dDevice;
};

} // namespace

Result print(QWidget *window, const QString &title, int pageCount, const PageImage &pageImage)
{
    if (!window || pageCount <= 0)
        return Result::Unavailable;
    const HWND hwnd = reinterpret_cast<HWND>(window->window()->winId());

    winrt::com_ptr<IPrintManagerInterop> interop;
    wgp::PrintManager manager{nullptr};
    try {
        interop = winrt::get_activation_factory<wgp::PrintManager, IPrintManagerInterop>();
        winrt::check_hresult(interop->GetForWindow(hwnd, winrt::guid_of<wgp::PrintManager>(), winrt::put_abi(manager)));
    } catch (...) {
        return Result::Unavailable;
    }

    struct State {
        std::atomic<bool> done{false};
        std::atomic<bool> taskCreated{false};
        std::atomic<int> completion{-1};
        QPointer<QEventLoop> loop;
        void finish()
        {
            done = true;
            if (loop)
                QMetaObject::invokeMethod(loop, &QEventLoop::quit, Qt::QueuedConnection);
        }
    };
    auto state = std::make_shared<State>();
    const wgp::IPrintDocumentSource source = winrt::make<DocumentSource>(pageCount, pageImage);
    const winrt::hstring jobTitle{reinterpret_cast<const wchar_t *>(title.utf16()), uint32_t(title.size())};

    const winrt::event_token token = manager.PrintTaskRequested(
        [state, source, jobTitle](const wgp::PrintManager &, const wgp::PrintTaskRequestedEventArgs &args) {
            const wgp::PrintTask task = args.Request().CreatePrintTask(
                jobTitle, [source](const wgp::PrintTaskSourceRequestedArgs &a) { a.SetSource(source); });
            try {
                // Let people print a range of pages.
                const auto ranges = task.Options().PageRangeOptions();
                ranges.AllowAllPages(true);
                ranges.AllowCustomSetOfPages(true);
                task.Options().DisplayedOptions().Append(wgp::StandardPrintTaskOptions::CustomPageRanges());
            } catch (...) {
            }
            task.Completed([state](const wgp::PrintTask &, const wgp::PrintTaskCompletedEventArgs &e) {
                state->completion = int(e.Completion());
                state->finish();
            });
            state->taskCreated = true;
        });

    Result result = Result::Unavailable;
    try {
        wf::IAsyncOperation<bool> op{nullptr};
        winrt::check_hresult(
            interop->ShowPrintUIForWindowAsync(hwnd, winrt::guid_of<wf::IAsyncOperation<bool>>(), winrt::put_abi(op)));
        op.Completed([state](const wf::IAsyncOperation<bool> &o, wf::AsyncStatus status) {
            // If the dialog never got as far as a print task, nothing else will end the wait.
            bool shown = false;
            try {
                shown = status == wf::AsyncStatus::Completed && o.GetResults();
            } catch (...) {
            }
            if (!shown || !state->taskCreated)
                state->finish();
        });
        QEventLoop loop;
        state->loop = &loop;
        if (!state->done)
            loop.exec(QEventLoop::ExcludeUserInputEvents);
        state->loop = nullptr;

        switch (state->completion.load()) {
        case int(wgp::PrintTaskCompletion::Submitted): result = Result::Printed; break;
        case int(wgp::PrintTaskCompletion::Failed): result = Result::Failed; break;
        case -1: result = state->taskCreated ? Result::Cancelled : Result::Unavailable; break;
        default: result = Result::Cancelled; break;
        }
    } catch (...) {
        result = state->taskCreated ? Result::Failed : Result::Unavailable;
    }
    manager.PrintTaskRequested(token);
    return result;
}

} // namespace WinPrint
