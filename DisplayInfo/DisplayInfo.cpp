#include "DisplayInfo.h"

namespace WPEFramework {
namespace Plugin {

    SERVICE_REGISTRATION(DisplayInfo, 1, 0);

    static Core::ProxyPoolType<Web::Response> responseFactory(4);
    static Core::ProxyPoolType<Web::JSONBodyType<JsonData::DisplayInfo::DisplayinfoData>> jsonResponseFactory(4);

    /* virtual */ const string DisplayInfo::Initialize(PluginHost::IShell* service)
    {
        ASSERT(service != nullptr);
        ASSERT(_device == nullptr);

        Config config;
        config.FromString(service->ConfigLine());
        _skipURL = static_cast<uint8_t>(service->WebPrefix().length());

        _device = IDeviceProperties::Instance();
        _device->AddRef();

        // On success return empty, to indicate there is no error text.
        return (EMPTY_STRING);
    }

    /* virtual */ void DisplayInfo::Deinitialize(PluginHost::IShell* service)
    {
        ASSERT(_device != nullptr);
        if (_device != nullptr) {
            _device->Release();
        }
    }

    /* virtual */ string DisplayInfo::Information() const
    {
        // No additional info to report.
        return (string());
    }

    /* virtual */ void DisplayInfo::Inbound(Web::Request& /* request */)
    {
    }

    /* virtual */ Core::ProxyType<Web::Response> DisplayInfo::Process(const Web::Request& request)
    {
        ASSERT(_skipURL <= request.Path.length());

        Core::ProxyType<Web::Response> result(PluginHost::Factories::Instance().Response());

        // By default, we assume everything works..
        result->ErrorCode = Web::STATUS_OK;
        result->Message = "OK";

        // <GET> - currently, only the GET command is supported, returning system info
        if (request.Verb == Web::Request::HTTP_GET) {

            Core::ProxyType<Web::JSONBodyType<JsonData::DisplayInfo::DisplayinfoData>> response(jsonResponseFactory.Element());

            Core::TextSegmentIterator index(Core::TextFragment(request.Path, _skipURL, static_cast<uint32_t>(request.Path.length()) - _skipURL), false, '/');

            // Always skip the first one, it is an empty part because we start with a '/' if there are more parameters.
            index.Next();

            Info(*response);
            result->ContentType = Web::MIMETypes::MIME_JSON;
            result->Body(Core::proxy_cast<Web::IBody>(response));
        } else {
            result->ErrorCode = Web::STATUS_BAD_REQUEST;
            result->Message = _T("Unsupported request for the [DisplayInfo] service.");
        }

        return result;
    }

    void DisplayInfo::Info(JsonData::DisplayInfo::DisplayinfoData& displayInfo) const
    {
        displayInfo.Firmwareversion = _device->FirmwareVersion();
        displayInfo.Chipset = _device->Chipset();

        IGraphicsProperties* graphics(_device->GraphicsInstance());
        displayInfo.Totalgpuram = graphics->TotalGpuRam();
        displayInfo.Freegpuram = graphics->FreeGpuRam();

        IConnectionProperties* connection(_device->ConnectionInstance());
        displayInfo.Audiopassthrough = connection->IsAudioPassthrough();
        displayInfo.Connected = connection->Connected();
        displayInfo.Width = connection->Width();
        displayInfo.Height = connection->Height();
        displayInfo.Hdcpmajor = connection->HDCPMajor();
        displayInfo.Hdcpminor = connection->HDCPMinor();
        displayInfo.Hdrtype = static_cast<JsonData::DisplayInfo::DisplayinfoData::HdrtypeType>(connection->Type());
    }

} // namespace Plugin
} // namespace WPEFramework
