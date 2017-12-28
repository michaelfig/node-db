// Copyright 2011 Mariano Iglesias <mgiglesias@gmail.com>
#include "./binding.h"

using namespace Napi;

node_db::Binding::Binding(const CallbackInfo& args): connection(NULL) {
}

node_db::Binding::~Binding() {
}

uv_async_t node_db::Binding::g_async;

Value node_db::Binding::Connect(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);

  node_db::Binding* binding = this;
    assert(binding);

    bool async = true;
    int optionsIndex = -1, callbackIndex = -1;

    if (args.Length() > 0) {
        if (args.Length() > 1) {
            ARG_CHECK_OBJECT(0, options);
            ARG_CHECK_FUNCTION(1, callback);
            optionsIndex = 0;
            callbackIndex = 1;
        } else if (args[0].IsFunction()) {
            ARG_CHECK_FUNCTION(0, callback);
            callbackIndex = 0;
        } else {
            ARG_CHECK_OBJECT(0, options);
            optionsIndex = 0;
        }

        if (optionsIndex >= 0) {
            Napi::Object options = args[optionsIndex].ToObject();

	    Napi::Value set = binding->set(options);
            if (!set.IsEmpty()) {
                return scope.Escape(set);
            }

            ARG_CHECK_OBJECT_ATTR_OPTIONAL_BOOL(options, async);

            if (options.Has(async_key) && !options.Get(async_key).ToBoolean().Value()) {
                async = false;
            }
        }

        if (callbackIndex >= 0) {
	  binding->cbConnect.Reset(args[callbackIndex].As<Function>());
        }
    }

    connect_request_t* request = new connect_request_t();
    if (request == NULL) {
        THROW_EXCEPTION("Could not create EIO request")
    }

    request->context = Persistent(args.This().As<Object>());
    request->binding = binding;
    request->error = NULL;

    if (async) {
        request->binding->Ref();

        uv_work_t* req = new uv_work_t();
        req->data = request;
        uv_queue_work(uv_default_loop(), req, uvConnect, (uv_after_work_cb)uvConnectFinished);

	uv_ref((uv_handle_t *)&g_async);

    } else {
        connect(request);
        connectFinished(request);
    }

    return scope.Escape(env.Undefined());
}

void node_db::Binding::connect(connect_request_t* request) {
    try {
        request->binding->connection->open();
    } catch(node_db::Exception const& exception) {
        request->error = exception.what();
    }
}

void node_db::Binding::connectFinished(connect_request_t* request) {
  Napi::Env env = request->context.Env();
    bool connected = request->binding->connection->isAlive();
    Napi::Value argv[2];

    if (connected) {
        Napi::Object server = Object::New(env);
        server.Set(String::New(env, "version"), String::New(env, request->binding->connection->version().c_str()));
        server.Set(String::New(env, "hostname"), String::New(env, request->binding->connection->getHostname().c_str()));
        server.Set(String::New(env, "user"), String::New(env, request->binding->connection->getUser().c_str()));
        server.Set(String::New(env, "database"), String::New(env, request->binding->connection->getDatabase().c_str()));

        argv[0] = env.Null();
        argv[1] = server;

        request->binding->Emit("ready", 1, &argv[1]);
    } else {
      argv[0] = String::New(env, request->error != NULL ? request->error : "(unknown error)");

        request->binding->Emit("error", 1, argv);
    }

    if (!request->binding->cbConnect.IsEmpty()) {
      try {
	if (connected) {
	  request->binding->cbConnect.Call(request->context.Value(), {argv[0], argv[1]});
	} else {
	  request->binding->cbConnect.Call(request->context.Value(), {argv[0]});
	}
      } catch (const Napi::Error& e) {
        e.Fatal("node_db::Binding::connectFinished", e.what());
      }
    }

    request->context.Reset();

    delete request;
}

void node_db::Binding::uvConnect(uv_work_t* uvRequest) {
    connect_request_t* request = static_cast<connect_request_t*>(uvRequest->data);
    assert(request);

    connect(request);
}

void node_db::Binding::uvConnectFinished(uv_work_t* uvRequest, int status) {
    connect_request_t* request = static_cast<connect_request_t*>(uvRequest->data);
    assert(request);

    Napi::Env env = request->context.Env();
    HandleScope scope(env);

    uv_unref((uv_handle_t *)&g_async);

    request->binding->Unref();

    connectFinished(request);
}

Value node_db::Binding::Disconnect(const CallbackInfo& args) {
  Napi::Env env = args.Env();
    EscapableHandleScope scope(env);

    node_db::Binding* binding = this;
    assert(binding);

    binding->connection->close();

    return scope.Escape(env.Undefined());
}

Value node_db::Binding::IsConnected(const CallbackInfo& args) {
  Napi::Env env = args.Env();
    EscapableHandleScope scope(env);

    node_db::Binding* binding = this;
    assert(binding);

    return scope.Escape(Boolean::New(env, binding->connection->isAlive(true)));
}

Value node_db::Binding::Escape(const CallbackInfo& args) {
  Napi::Env env = args.Env();
    EscapableHandleScope scope(env);

    ARG_CHECK_STRING(0, string);

    node_db::Binding* binding = this;
    assert(binding);

    std::string escaped;

    try {
        Napi::String string(env, args[0].ToString());
        std::string unescaped(string);
        escaped = binding->connection->escape(unescaped);
    } catch(node_db::Exception const& exception) {
        THROW_EXCEPTION(exception.what())
    }

    return scope.Escape(String::New(env, escaped.c_str()));
}

Value node_db::Binding::Name(const CallbackInfo& args) {
  Napi::Env env = args.Env();
    EscapableHandleScope scope(env);

    ARG_CHECK_STRING(0, table);

    node_db::Binding* binding = this;
    assert(binding);

    std::ostringstream escaped;

    try {
        Napi::String string(env, args[0].ToString());
        std::string unescaped(string);
        escaped << binding->connection->escapeName(unescaped);
    } catch(node_db::Exception const& exception) {
        THROW_EXCEPTION(exception.what())
    }

    return scope.Escape(String::New(env, escaped.str().c_str()));
}

Value node_db::Binding::Query(const CallbackInfo& args) {
  Napi::Env env = args.Env();
    EscapableHandleScope scope(env);

    node_db::Binding* binding = this;
    assert(binding);

    ObjectReference query = binding->createQuery();
    if (query.IsEmpty()) {
        THROW_EXCEPTION("Could not create query");
    }

    node_db::Query* queryInstance;
    if (napi_unwrap(query.Env(), query.Value(), reinterpret_cast<void**>(&queryInstance)) != napi_ok) {
      throw Error::New(query.Env());
    }

    queryInstance->setConnection(binding->connection);

    Napi::Value set = queryInstance->set(args);
    if (!set.IsEmpty()) {
        return scope.Escape(set);
    }

    return scope.Escape(query.Value());
}
