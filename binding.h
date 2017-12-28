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
#define BINDING_PROPS					     \
    XC(COLUMN_TYPE_STRING, node_db::Result::Column::STRING), \
    XC(COLUMN_TYPE_BOOL, node_db::Result::Column::BOOL), \
    XC(COLUMN_TYPE_INT, node_db::Result::Column::INT), \
    XC(COLUMN_TYPE_BIGINT, node_db::Result::Column::BIGINT), \
    XC(COLUMN_TYPE_NUMBER, node_db::Result::Column::NUMBER), \
    XC(COLUMN_TYPE_DATE, node_db::Result::Column::DATE), \
    XC(COLUMN_TYPE_TIME, node_db::Result::Column::TIME), \
    XC(COLUMN_TYPE_DATETIME, node_db::Result::Column::DATETIME), \
    XC(COLUMN_TYPE_TEXT, node_db::Result::Column::TEXT), \
    XC(COLUMN_TYPE_SET, node_db::Result::Column::SET), \
    XM("connect", Connect), \
    XM("disconnect", Disconnect), \
    XM("isConnected", IsConnected), \
    XM("escape", Escape), \
    XM("name", Name), \
    XM("query", Query)

        Napi::Value Connect(const CallbackInfo& args);
        Napi::Value Disconnect(const CallbackInfo& args);
        Napi::Value IsConnected(const CallbackInfo& args);
        Napi::Value Escape(const CallbackInfo& args);
        Napi::Value Name(const CallbackInfo& args);
        Napi::Value Query(const CallbackInfo& args);
	static uv_async_t g_async;
        static void uvConnect(uv_work_t* uvRequest);
        static void uvConnectFinished(uv_work_t* uvRequest, int status);
        static void connect(connect_request_t* request);
        static void connectFinished(connect_request_t* request);
        virtual Napi::Value set(const Napi::Object options) = 0;
        virtual ObjectReference createQuery() const = 0;
	virtual void Ref() = 0;
	virtual void Unref() = 0;
};
}

#endif  // BINDING_H_
