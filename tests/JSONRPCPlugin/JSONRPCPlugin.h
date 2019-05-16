#pragma once

#include "Data.h"
#include "Module.h"

#include <interfaces/IPerformance.h>

namespace WPEFramework {

namespace Plugin {

    // This is a server for a JSONRPC communication channel.
    // For a plugin to be capable to handle JSONRPC, inherit from PluginHost::JSONRPC.
    // By inheriting from this class, the plugin realizes the interface PluginHost::IDispatcher.
    // This realization of this interface implements, by default, the following methods on this plugin
    // - exists
    // - register
    // - unregister
    // Any other methood to be handled by this plugin  can be added can be added by using the
    // templated methods Rgister on the PluginHost::JSONRPC class.
    // As the registration/unregistration of notifications is realized by the class PluginHost::JSONRPC,
    // this class exposes a public method called, Notify(), using this methods, all subscribed clients
    // will receive a JSONRPC message as a notification, in case this method is called.
    class JSONRPCPlugin : public PluginHost::IPlugin, public PluginHost::JSONRPC, public Exchange::IPerformance {
    private:
        // We do not allow this plugin to be copied !!
        JSONRPCPlugin(const JSONRPCPlugin&) = delete;
        JSONRPCPlugin& operator=(const JSONRPCPlugin&) = delete;

        // The next class describes configuration information for this plugin.
        class Config : public Core::JSON::Container {
        private:
            Config(const Config&) = delete;
            Config& operator=(const Config&) = delete;

        public:
            Config()
                : Core::JSON::Container()
                , Connector("0.0.0.0:8899")
            {
                Add(_T("connector"), &Connector);
            }
            ~Config()
            {
            }

        public:
            Core::JSON::String Connector;
        };

        // For measuring differnt performance times, also expose a COMRPC server. The transport protocol is binairy and the invocation of the
        // calls requires less thread switches.
        // The following class exposes a server that exposes the Exchange::IRPCLink interface to be retrieved from another process. The client
        // process can than measure the overhead of the call towards the Exchange::IRPCLink methods.
        // The processing time of these methods will also be acounted for internally and "send" back as well
        class COMServer : public RPC::Communicator {
        private:
            COMServer() = delete;
            COMServer(const COMServer&) = delete;
            COMServer& operator=(const COMServer&) = delete;

        public:
            COMServer(const Core::NodeId& source, Exchange::IPerformance* parentInterface, const string& proxyStubPath)
                : RPC::Communicator(source, Core::ProxyType<RPC::InvokeServerType<4, 1>>::Create(), proxyStubPath)
                , _parentInterface(parentInterface)
            {
                Open(Core::infinite);
            }
            ~COMServer()
            {
                Close(Core::infinite);
            }

        private:
            virtual void* Aquire(const string& className, const uint32_t interfaceId, const uint32_t versionId)
            {
                void* result = nullptr;

                // Currently we only support version 1 of the IRPCLink :-)
                if (((versionId == 1) || (versionId == static_cast<uint32_t>(~0))) && ((interfaceId == Exchange::IPerformance::ID) || (interfaceId == Core::IUnknown::ID))) {
                    // Reference count our parent
                    _parentInterface->AddRef();

                    // Allright, respond with the interface.
                    result = _parentInterface;

					printf("Pointer => %p\n", result);
                }
                return (result);
            }

        private:
            Exchange::IPerformance* _parentInterface;
        };

        // The next class is a helper class, just to trigger an a-synchronous callback every Period()
        // amount of time.
        class PeriodicSync : public Core::IDispatch {
        private:
            PeriodicSync() = delete;
            PeriodicSync(const PeriodicSync&) = delete;
            PeriodicSync& operator=(const PeriodicSync&) = delete;

        public:
            PeriodicSync(JSONRPCPlugin* parent)
                : _parent(*parent)
            {
            }
            ~PeriodicSync()
            {
            }

        public:
            void Period(const uint8_t time)
            {
                _nextSlot = (time * 1000);
            }
            // This method is called by the main process ThreadPool at the scheduled time.
            // After the parent has been called to send out a-synchronous notifications, it
            // will schedule itself again, to be triggered after the set period.
            virtual void Dispatch() override
            {
                _parent.SendTime();

                if (_nextSlot != 0) {
                    PluginHost::WorkerPool::Instance().Schedule(Core::Time::Now().Add(_nextSlot), Core::ProxyType<Core::IDispatch>(*this));
                }
            }

        private:
            uint32_t _nextSlot;
            JSONRPCPlugin& _parent;
        };
        class Callback : public Core::IDispatch {
        private:
            Callback() = delete;
            Callback(const Callback&) = delete;
            Callback& operator=(const Callback&) = delete;

        public:
            Callback(JSONRPCPlugin* parent, const Core::JSONRPC::Connection& channel)
                : _parent(*parent)
                , _channel(channel)
            {
            }
            ~Callback()
            {
            }

        public:
            // This method is called by the main process ThreadPool at the scheduled time.
            // After the parent has been called to send out a-synchronous notifications, it
            // will schedule itself again, to be triggered after the set period.
            virtual void Dispatch() override
            {
                _parent.SendTime(_channel);
            }

        private:
            JSONRPCPlugin& _parent;
            Core::JSONRPC::Connection _channel;
        };

        // Define a handler for incoming JSONRPC messages. This method does not take any
        // parameters, it just returns the current time of this server, if it is called.
        uint32_t time(Core::JSON::String& response)
        {
            response = Core::Time::Now().ToRFC1123();
            return (Core::ERROR_NONE);
        }

        uint32_t postmessage(const Data::MessageParameters& params)
        {
            PostMessage(params.Recipient.Value(), params.Message.Value());
            return (Core::ERROR_NONE);
        }

        uint32_t clueless()
        {
            TRACE(Trace::Information, (_T("A parameter less method that returns nothing was triggered")));
            return (Core::ERROR_NONE);
        }

        uint32_t input(const Core::JSON::String& info)
        {
            TRACE(Trace::Information, (_T("Received the text: %s"), info.Value().c_str()));
            return (Core::ERROR_NONE);
        }

        // If the parameters are more complex (aggregated JSON objects) us JSON container
        // classes.
        uint32_t extended(const Data::Parameters& params, Data::Response& response)
        {
            if (params.UTC.Value() == true) {
                response.Time = Core::Time::Now().Ticks();
            } else {
                response.Time = Core::Time::Now().Add(60 * 60 * 100).Ticks();
            }
            if (params.Location.Value() == _T("BadDay")) {
                response.State = Data::Response::INACTIVE;
            } else {
                response.State = Data::Response::IDLE;
            }
            return (Core::ERROR_NONE);
        }
        uint32_t set_geometry(const Data::Geometry& window)
        {
            _window.X = window.X.IsSet() ? window.X.Value() : _window.X;
            _window.Y = window.Y.IsSet() ? window.Y.Value() : _window.Y;
            _window.Width = window.Width;
            _window.Height = window.Height;
            return (Core::ERROR_NONE);
        }
        uint32_t get_geometry(Data::Geometry& window) const
        {
            window = Data::Geometry(_window.X, _window.Y, _window.Width, _window.Height);
            return (Core::ERROR_NONE);
        }
        uint32_t get_data(Core::JSON::String& data) const
        {
            data = _data;
            return (Core::ERROR_NONE);
        }
        uint32_t set_data(const Core::JSON::String& data)
        {
            _data = data.Value();
            return (Core::ERROR_NONE);
        }
        uint32_t swap(const JsonObject& parameters, JsonObject& response)
        {
            response = JsonObject({ { "x", 111 }, { "y", 222 }, { "width", _window.Width }, { "height", _window.Height } });

            // Now lets see what we got we can set..
            Core::JSON::Variant value = parameters.Get("x");
            if (value.Content() == Core::JSON::Variant::type::NUMBER) {
                _window.X = static_cast<uint32_t>(value.Number());
            } else if (value.Content() == Core::JSON::Variant::type::EMPTY) {
                TRACE(Trace::Information, ("The <x> is not available"));
            } else {
                TRACE(Trace::Information, ("The <x> is not defined as a number"));
            }
            value = parameters.Get("y");
            if (value.Content() == Core::JSON::Variant::type::NUMBER) {
                _window.X = static_cast<uint32_t>(value.Number());
            }
            value = parameters.Get("width");
            if (value.Content() == Core::JSON::Variant::type::NUMBER) {
                _window.X = static_cast<uint32_t>(value.Number());
            }
            value = parameters.Get("height");
            if (value.Content() == Core::JSON::Variant::type::NUMBER) {
                _window.X = static_cast<uint32_t>(value.Number());
            }
            return (Core::ERROR_NONE);
        }
        uint32_t set_opaque_geometry(const JsonObject& window)
        {
            // Now lets see what we got we can set..
            Core::JSON::Variant value = window.Get("x");
            if (value.Content() == Core::JSON::Variant::type::NUMBER) {
                _window.X = static_cast<uint32_t>(value.Number());
            } else if (value.Content() == Core::JSON::Variant::type::EMPTY) {
                TRACE(Trace::Information, ("The <x> is not available"));
            } else {
                TRACE(Trace::Information, ("The <x> is not defined as a number"));
            }
            value = window.Get("y");
            if (value.Content() == Core::JSON::Variant::type::NUMBER) {
                _window.Y = static_cast<uint32_t>(value.Number());
            }
            value = window.Get("width");
            if (value.Content() == Core::JSON::Variant::type::NUMBER) {
                _window.Width = static_cast<uint32_t>(value.Number());
            }
            value = window.Get("height");
            if (value.Content() == Core::JSON::Variant::type::NUMBER) {
                _window.Height = static_cast<uint32_t>(value.Number());
            }
            return (Core::ERROR_NONE);
        }
        uint32_t get_opaque_geometry(JsonObject& window) const
        {
            window = JsonObject({ { "x", _window.X }, { "y", _window.Y }, { "width", _window.Width }, { "height", _window.Height } });
            return (Core::ERROR_NONE);
        }
        void async_callback(const Core::JSONRPC::Connection& connection, const Core::JSON::DecUInt8& seconds)
        {
            Core::ProxyType<Callback> job(Core::ProxyType<Callback>::Create(this, connection));
            PluginHost::WorkerPool::Instance().Schedule(Core::Time::Now().Add(seconds * 1000), job);
        }

		// Methods for performance measurements
		uint32_t send(const Data::JSONDataBuffer& data, Core::JSON::DecUInt32& result) 
		{
            uint16_t length = (((data.Data.Value().length() * 6) + 7) / 8);
            uint8_t* buffer = static_cast<uint8_t*>(ALLOCA(length));
            Core::FromString(data.Data.Value(), buffer, length);
            result = Send(length, buffer); 
            return (Core::ERROR_NONE);
        }
        uint32_t receive(const Core::JSON::DecUInt16& maxSize, Data::JSONDataBuffer& data)
        {
            string convertedBuffer;
            uint16_t length = maxSize.Value();
            uint8_t* buffer = static_cast<uint8_t*>(ALLOCA(length));
            data.Duration = Receive(length, buffer);
            Core::ToString(buffer, length, false, convertedBuffer);
            data.Data = convertedBuffer;

            return (Core::ERROR_NONE);
        }
        uint32_t exchange(const Data::JSONDataBuffer& data, Data::JSONDataBuffer& result)
        {
            string convertedBuffer;
            uint16_t length = (((data.Data.Value().length() * 6) + 7) / 8);
            uint8_t* buffer = static_cast<uint8_t*>(ALLOCA(length));
            Core::FromString(data.Data.Value(), buffer, length);
            result.Duration = Exchange(length, buffer, data.Length.Value());
            Core::ToString(buffer, length, false, convertedBuffer);
            result.Data = convertedBuffer;

            return (Core::ERROR_NONE);
        }

    public:
        JSONRPCPlugin();
        virtual ~JSONRPCPlugin();

        // Build QueryInterface implementation, specifying all possible interfaces to be returned.
        BEGIN_INTERFACE_MAP(JSONRPCPlugin)
        INTERFACE_ENTRY(PluginHost::IPlugin)
        INTERFACE_ENTRY(PluginHost::IDispatcher)
        INTERFACE_ENTRY(Exchange::IPerformance)
        END_INTERFACE_MAP

    public:
        //   IPlugin methods
        // -------------------------------------------------------------------------------------------------------
        virtual const string Initialize(PluginHost::IShell* service) override;
        virtual void Deinitialize(PluginHost::IShell* service) override;
        virtual string Information() const override;

        //   Private methods specific to this class.
        // -------------------------------------------------------------------------------------------------------
        void PostMessage(const string& recipient, const string& message);
        void SendTime();
        void SendTime(Core::JSONRPC::Connection& channel);

        //   Exchange::IPerformance methods
        // -------------------------------------------------------------------------------------------------------
        virtual uint32_t Send(const uint16_t sendSize, const uint8_t buffer[]) override;
        virtual uint32_t Receive(uint16_t& bufferSize, uint8_t buffer[]) const override;
        virtual uint32_t Exchange(uint16_t& bufferSize, uint8_t buffer[], const uint16_t maxBufferSize) override;

    private:
        Core::ProxyType<PeriodicSync> _job;
        Data::Window _window;
        string _data;
        COMServer* _rpcServer;
    };

} // namespace Plugin
} // namespace WPEFramework
