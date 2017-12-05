// Copyright 2011 Mariano Iglesias <mgiglesias@gmail.com>
#ifndef BINDING_H_
#define BINDING_H_

#include <napi.h>
#include <uv.h>
#include <string>
#include "./node_defs.h"
#include "./connection.h"
#include "./events.h"
#include "./exception.h"
#include "./query.h"

namespace node_db {
  using namespace Napi;

  class Binding : public EventEmitter {
    public:
        Connection* connection;

    protected:
        struct connect_request_t {
            ObjectReference context;
            Binding* binding;
            const char* error;
        };
        FunctionReference cbConnect;

        Binding(const CallbackInfo& args);
        ~Binding();
        static void Init(Object target, Napi::FunctionReference constructorTemplate);
        static Napi::Value Connect(const CallbackInfo& args);
        static Napi::Value Disconnect(const CallbackInfo& args);
        static Napi::Value IsConnected(const CallbackInfo& args);
        static Napi::Value Escape(const CallbackInfo& args);
        static Napi::Value Name(const CallbackInfo& args);
        static Napi::Value Query(const CallbackInfo& args);
	static uv_async_t g_async;
        static void uvConnect(uv_work_t* uvRequest);
        static void uvConnectFinished(uv_work_t* uvRequest, int status);
        static void connect(connect_request_t* request);
        static void connectFinished(connect_request_t* request);
        virtual Napi::Value set(const Napi::Object options) = 0;
        virtual ObjectReference createQuery() const = 0;
};
}

#endif  // BINDING_H_
