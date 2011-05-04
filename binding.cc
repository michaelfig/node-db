// Copyright 2011 Mariano Iglesias <mgiglesias@gmail.com>
#include "./binding.h"

v8::Persistent<v8::String> node_db::Binding::syReady;
v8::Persistent<v8::String> node_db::Binding::syError;

node_db::Binding::Binding(): node::EventEmitter(), connection(NULL) {
}

node_db::Binding::~Binding() {
}

void node_db::Binding::Init(v8::Handle<v8::Object> target, v8::Persistent<v8::FunctionTemplate> constructorTemplate) {
    NODE_ADD_CONSTANT(constructorTemplate, COLUMN_TYPE_STRING, node_db::Result::Column::STRING);
    NODE_ADD_CONSTANT(constructorTemplate, COLUMN_TYPE_BOOL, node_db::Result::Column::BOOL);
    NODE_ADD_CONSTANT(constructorTemplate, COLUMN_TYPE_INT, node_db::Result::Column::INT);
    NODE_ADD_CONSTANT(constructorTemplate, COLUMN_TYPE_NUMBER, node_db::Result::Column::NUMBER);
    NODE_ADD_CONSTANT(constructorTemplate, COLUMN_TYPE_DATE, node_db::Result::Column::DATE);
    NODE_ADD_CONSTANT(constructorTemplate, COLUMN_TYPE_TIME, node_db::Result::Column::TIME);
    NODE_ADD_CONSTANT(constructorTemplate, COLUMN_TYPE_DATETIME, node_db::Result::Column::DATETIME);
    NODE_ADD_CONSTANT(constructorTemplate, COLUMN_TYPE_TEXT, node_db::Result::Column::TEXT);
    NODE_ADD_CONSTANT(constructorTemplate, COLUMN_TYPE_SET, node_db::Result::Column::SET);

    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "connect", Connect);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "disconnect", Disconnect);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "isConnected", IsConnected);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "escape", Escape);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "table", Table);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "field", Field);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "query", Query);

    syReady = NODE_PERSISTENT_SYMBOL("ready");
    syError = NODE_PERSISTENT_SYMBOL("error");
}

v8::Handle<v8::Value> node_db::Binding::Connect(const v8::Arguments& args) {
    v8::HandleScope scope;

    node_db::Binding* binding = node::ObjectWrap::Unwrap<node_db::Binding>(args.This());
    assert(binding);

    bool async = true;

    if (args.Length() > 0) {
        v8::Handle<v8::Value> set = binding->set(args);
        if (!set.IsEmpty()) {
            return scope.Close(set);
        }

        v8::Local<v8::Object> options = args[0]->ToObject();

        ARG_CHECK_OBJECT_ATTR_OPTIONAL_BOOL(options, async);

        if (options->Has(async_key) && options->Get(async_key)->IsFalse()) {
            async = false;
        }
    }

    connect_request_t* request = new connect_request_t();
    if (request == NULL) {
        THROW_EXCEPTION("Could not create EIO request")
    }

    request->binding = binding;
    request->error = NULL;

    if (async) {
        request->binding->Ref();
        eio_custom(eioConnect, EIO_PRI_DEFAULT, eioConnectFinished, request);
        ev_ref(EV_DEFAULT_UC);
    } else {
        connect(request);
        connectFinished(request);
    }

    return scope.Close(v8::Undefined());
}

v8::Handle<v8::Value> node_db::Binding::set(const v8::Arguments& args) {
    assert(this->connection);

    ARG_CHECK_OBJECT(0, options);

    v8::Local<v8::Object> options = args[0]->ToObject();

    ARG_CHECK_OBJECT_ATTR_OPTIONAL_STRING(options, hostname);
    ARG_CHECK_OBJECT_ATTR_OPTIONAL_STRING(options, user);
    ARG_CHECK_OBJECT_ATTR_OPTIONAL_STRING(options, password);
    ARG_CHECK_OBJECT_ATTR_OPTIONAL_STRING(options, database);
    ARG_CHECK_OBJECT_ATTR_OPTIONAL_UINT32(options, port);

    v8::String::Utf8Value hostname(options->Get(hostname_key)->ToString());
    v8::String::Utf8Value user(options->Get(user_key)->ToString());
    v8::String::Utf8Value password(options->Get(password_key)->ToString());
    v8::String::Utf8Value database(options->Get(database_key)->ToString());

    if (options->Has(hostname_key)) {
        this->connection->setHostname(*hostname);
    }

    if (options->Has(user_key)) {
        this->connection->setUser(*user);
    }

    if (options->Has(password_key)) {
        this->connection->setPassword(*password);
    }

    if (options->Has(database_key)) {
        this->connection->setDatabase(*database);
    }

    if (options->Has(port_key)) {
        this->connection->setPort(options->Get(port_key)->ToInt32()->Value());
    }

    return v8::Handle<v8::Value>();
}

void node_db::Binding::connect(connect_request_t* request) {
    try {
        request->binding->connection->open();
    } catch(const node_db::Exception& exception) {
        request->error = exception.what();
    }
}

void node_db::Binding::connectFinished(connect_request_t* request) {
    bool connected = request->binding->connection->isOpened();

    if (connected) {
        v8::Local<v8::Object> server = v8::Object::New();
        server->Set(v8::String::New("version"), v8::String::New(request->binding->connection->version().c_str()));
        server->Set(v8::String::New("hostname"), v8::String::New(request->binding->connection->getHostname().c_str()));
        server->Set(v8::String::New("user"), v8::String::New(request->binding->connection->getUser().c_str()));
        server->Set(v8::String::New("database"), v8::String::New(request->binding->connection->getDatabase().c_str()));

        v8::Local<v8::Value> argv[1];
        argv[0] = server;

        request->binding->Emit(syReady, 1, argv);
    } else {
        v8::Local<v8::Value> argv[1];
        argv[0] = v8::String::New(request->error != NULL ? request->error : "(unknown error)");

        request->binding->Emit(syError, 1, argv);
    }

    delete request;
}

int node_db::Binding::eioConnect(eio_req* eioRequest) {
    connect_request_t* request = static_cast<connect_request_t*>(eioRequest->data);
    assert(request);

    connect(request);

    return 0;
}

int node_db::Binding::eioConnectFinished(eio_req* eioRequest) {
    v8::HandleScope scope;

    connect_request_t* request = static_cast<connect_request_t*>(eioRequest->data);
    assert(request);

    ev_unref(EV_DEFAULT_UC);
    request->binding->Unref();

    connectFinished(request);

    return 0;
}

v8::Handle<v8::Value> node_db::Binding::Disconnect(const v8::Arguments& args) {
    v8::HandleScope scope;

    node_db::Binding* binding = node::ObjectWrap::Unwrap<node_db::Binding>(args.This());
    assert(binding);

    binding->connection->close();

    return scope.Close(v8::Undefined());
}

v8::Handle<v8::Value> node_db::Binding::IsConnected(const v8::Arguments& args) {
    v8::HandleScope scope;

    node_db::Binding* binding = node::ObjectWrap::Unwrap<node_db::Binding>(args.This());
    assert(binding);

    return scope.Close(binding->connection->isOpened() ? v8::True() : v8::False());
}

v8::Handle<v8::Value> node_db::Binding::Escape(const v8::Arguments& args) {
    v8::HandleScope scope;

    ARG_CHECK_STRING(0, string);

    node_db::Binding* binding = node::ObjectWrap::Unwrap<node_db::Binding>(args.This());
    assert(binding);

    std::string escaped;

    try {
        v8::String::Utf8Value string(args[0]->ToString());
        std::string unescaped(*string);
        escaped = binding->connection->escape(unescaped);
    } catch(const node_db::Exception& exception) {
        THROW_EXCEPTION(exception.what())
    }

    return scope.Close(v8::String::New(escaped.c_str()));
}

v8::Handle<v8::Value> node_db::Binding::Table(const v8::Arguments& args) {
    v8::HandleScope scope;

    ARG_CHECK_STRING(0, table);

    node_db::Binding* binding = node::ObjectWrap::Unwrap<node_db::Binding>(args.This());
    assert(binding);

    std::ostringstream escaped;

    try {
        v8::String::Utf8Value string(args[0]->ToString());
        std::string unescaped(*string);
        escaped << binding->connection->quoteTable << unescaped << binding->connection->quoteTable;
    } catch(const node_db::Exception& exception) {
        THROW_EXCEPTION(exception.what())
    }

    return scope.Close(v8::String::New(escaped.str().c_str()));
}

v8::Handle<v8::Value> node_db::Binding::Field(const v8::Arguments& args) {
    v8::HandleScope scope;

    ARG_CHECK_STRING(0, field);

    node_db::Binding* binding = node::ObjectWrap::Unwrap<node_db::Binding>(args.This());
    assert(binding);

    std::ostringstream escaped;

    try {
        v8::String::Utf8Value string(args[0]->ToString());
        std::string unescaped(*string);
        escaped << binding->connection->quoteField << unescaped << binding->connection->quoteField;
    } catch(const node_db::Exception& exception) {
        THROW_EXCEPTION(exception.what())
    }

    return scope.Close(v8::String::New(escaped.str().c_str()));
}

v8::Handle<v8::Value> node_db::Binding::Query(const v8::Arguments& args) {
    v8::HandleScope scope;

    node_db::Binding* binding = node::ObjectWrap::Unwrap<node_db::Binding>(args.This());
    assert(binding);

    v8::Persistent<v8::Object> query = binding->createQuery();
    if (query.IsEmpty()) {
        THROW_EXCEPTION("Could not create query");
    }

    node_db::Query* queryInstance = node::ObjectWrap::Unwrap<node_db::Query>(query);
    queryInstance->setConnection(binding->connection);

    v8::Handle<v8::Value> set = queryInstance->set(args);
    if (!set.IsEmpty()) {
        return scope.Close(set);
    }

    return scope.Close(query);
}