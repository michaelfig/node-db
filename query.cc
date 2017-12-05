// Copyright 2011 Mariano Iglesias <mgiglesias@gmail.com>
// Copyright 2011 Georg Wicherski <gw@oxff.net>
#include "./query.h"
using namespace Napi;

bool node_db::Query::gmtDeltaLoaded = false;
int node_db::Query::gmtDelta;

uv_async_t node_db::Query::g_async;

Napi::String v8StringFromUInt64(const Napi::Env &env, uint64_t num, std::ostringstream &reusableStream) {
    reusableStream.clear();
    reusableStream.seekp(0);
    reusableStream << num << std::ends;
    return String::New(env, reusableStream.str().c_str());
}

void node_db::Query::Init(Object target, Napi::FunctionReference constructorTemplate) {
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "select", Select);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "from", From);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "join", Join);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "where", Where);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "and", And);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "or", Or);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "order", Order);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "limit", Limit);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "add", Add);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "insert", Insert);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "update", Update);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "set", Set);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "delete", Delete);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "sql", Sql);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "execute", Execute);
}

node_db::Query::Query(const CallbackInfo& args): node_db::EventEmitter(args),
    connection(NULL), async(true), cast(true), bufferText(false) {
}

node_db::Query::~Query() {
  for (std::vector< Reference<Napi::Value> >::iterator iterator = this->values.begin(), end = this->values.end(); iterator != end; ++iterator) {
        iterator->Reset();
    }
}

void node_db::Query::setConnection(node_db::Connection* connection) {
    this->connection = connection;
}

Napi::Value node_db::Query::Select(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);

    if (args.Length() > 0) {
        if (args[0].IsArray()) {
            ARG_CHECK_ARRAY(0, fields);
        } else if (args[0].IsObject()) {
            ARG_CHECK_OBJECT(0, fields);
        } else {
            ARG_CHECK_STRING(0, fields);
        }
    } else {
        ARG_CHECK_STRING(0, fields);
    }

    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(args.This().As<Object>()));
    assert(query);

    query->sql << "SELECT ";

    if (args[0].IsArray()) {
        Napi::Array fields = args[0].As<Napi::Array>();
        if (fields.Length() == 0) {
            THROW_EXCEPTION("No fields specified in select")
        }

        for (uint32_t i = 0, limiti = fields.Length(); i < limiti; i++) {
            if (i > 0) {
                query->sql << ",";
            }

            try {
	      query->sql << query->fieldName(env, fields.Get(i));
            } catch(const node_db::Exception& exception) {
                THROW_EXCEPTION(exception.what())
            }
        }
    } else if (args[0].IsObject()) {
        try {
	  query->sql << query->fieldName(env, args[0]);
        } catch(const node_db::Exception& exception) {
            THROW_EXCEPTION(exception.what())
        }
    } else {
        Napi::String fields(env, args[0].ToString());
        query->sql << fields;
    }

    return scope.Escape(args.This());
}

Napi::Value node_db::Query::From(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);

    if (args.Length() > 0) {
        if (args[0].IsArray()) {
            ARG_CHECK_ARRAY(0, fields);
        } else if (args[0].IsObject()) {
            ARG_CHECK_OBJECT(0, tables);
        } else {
            ARG_CHECK_STRING(0, tables);
        }
    } else {
        ARG_CHECK_STRING(0, tables);
    }

    ARG_CHECK_OPTIONAL_BOOL(1, escape);

    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(args.This().As<Object>()));
    assert(query);

    bool escape = true;
    if (args.Length() > 1) {
        escape = args[1].ToBoolean();
    }

    query->sql << " FROM ";

    try {
      query->sql << query->tableName(env, args[0], escape);
    } catch(const node_db::Exception& exception) {
        THROW_EXCEPTION(exception.what());
    }

    return scope.Escape(args.This());
}

Napi::Value node_db::Query::Join(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);

    ARG_CHECK_OBJECT(0, join);
    ARG_CHECK_OPTIONAL_ARRAY(1, values);

    Napi::Object join = args[0].ToObject();

    ARG_CHECK_OBJECT_ATTR_OPTIONAL_STRING(join, type);
    ARG_CHECK_OBJECT_ATTR_STRING(join, table);
    ARG_CHECK_OBJECT_ATTR_OPTIONAL_STRING(join, alias);
    ARG_CHECK_OBJECT_ATTR_OPTIONAL_STRING(join, conditions);
    ARG_CHECK_OBJECT_ATTR_OPTIONAL_BOOL(join, escape);

    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(args.This().As<Object>()));
    assert(query);

    std::string type = "INNER";
    bool escape = true;

    if (join.Has(type_key)) {
        Napi::String currentType(env, join.Get(type_key).ToString());
        type = currentType;
        std::transform(type.begin(), type.end(), type.begin(), toupper);
    }

    if (join.Has(escape_key)) {
        escape = join.Get(escape_key).ToBoolean();
    }

    Napi::String table(env, join.Get(table_key).ToString());

    query->sql << " " << type << " JOIN ";
    query->sql << (escape ? query->connection->escapeName(table) : table);

    if (join.Has(alias_key)) {
        Napi::String alias(env, join.Get(alias_key).ToString());
        query->sql << " AS ";
        query->sql << (escape ? query->connection->escapeName(alias) : alias);
    }

    if (join.Has(conditions_key)) {
        Napi::String conditions(env, join.Get(conditions_key).ToObject());
        std::string currentConditions = conditions;
        if (args.Length() > 1) {
            Napi::Array currentValues = args[1].As<Napi::Array>();
            for (uint32_t i = 0, limiti = currentValues.Length(); i < limiti; i++) {
	      query->values.push_back(Persistent(currentValues.Get(i)));
            }
        }

        query->sql << " ON (" << currentConditions << ")";
    }

    return scope.Escape(args.This());
}

Napi::Value node_db::Query::Where(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);

    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(args.This().As<Object>()));
    assert(query);

    return scope.Escape(query->addCondition(args, "WHERE"));
}

Napi::Value node_db::Query::And(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);

    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(args.This().As<Object>()));
    assert(query);

    return scope.Escape(query->addCondition(args, "AND"));
}

Napi::Value node_db::Query::Or(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);

    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(args.This().As<Object>()));
    assert(query);

    return scope.Escape(query->addCondition(args, "OR"));
}

Napi::Value node_db::Query::Order(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);

    if (args.Length() > 0 && args[0].IsObject()) {
        ARG_CHECK_OBJECT(0, fields);
    } else {
        ARG_CHECK_STRING(0, fields);
    }

    ARG_CHECK_OPTIONAL_BOOL(1, escape);

    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(args.This().As<Object>()));
    assert(query);

    bool escape = true;
    if (args.Length() > 1) {
        escape = args[1].ToBoolean();
    }

    query->sql << " ORDER BY ";

    if (args[0].IsObject()) {
        Napi::Object fields = args[0].ToObject();
        Napi::Array properties = fields.GetPropertyNames();
        if (properties.Length() == 0) {
            THROW_EXCEPTION("Non empty objects should be used for fields in order");
        }

        for (uint32_t i = 0, limiti = properties.Length(); i < limiti; i++) {
            Napi::Value propertyName = properties.Get(i);
            Napi::String fieldName(env, propertyName);
            Napi::Value currentValue = fields.Get(propertyName);

            if (i > 0) {
                query->sql << ",";
            }

            bool innerEscape = escape;
            Napi::Value order;
            if (currentValue.IsObject()) {
                Napi::Object currentObject = currentValue.ToObject();
                Napi::String escapeKey = String::New(env, "escape");
                Napi::String orderKey = String::New(env, "order");
                Napi::Value optionValue;

                if (!currentObject.Has(orderKey)) {
                    THROW_EXCEPTION("The \"order\" option for the order field object must be specified");
                }

                order = currentObject.Get(orderKey);

                if (currentObject.Has(escapeKey)) {
                    optionValue = currentObject.Get(escapeKey);
                    if (!optionValue.IsBoolean()) {
                        THROW_EXCEPTION("Specify a valid boolean value for the \"escape\" option in the order field object");
                    }
                    innerEscape = optionValue.ToBoolean();
                }
            } else {
                order = currentValue;
            }

            query->sql << (innerEscape ? query->connection->escapeName(fieldName) : fieldName);
            query->sql << " ";

            if (order.IsBoolean()) {
                query->sql << (order.ToBoolean() ? "ASC" : "DESC");
            } else if (order.IsString()) {
                Napi::String currentOrder(env, order.ToString());
                query->sql << currentOrder;
            } else {
                THROW_EXCEPTION("Invalid value specified for \"order\" property in order field");
            }
        }
    } else {
        Napi::String sql(env, args[0].ToString());
        query->sql << sql;
    }

    return scope.Escape(args.This());
}

Napi::Value node_db::Query::Limit(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);

    if (args.Length() > 1) {
        ARG_CHECK_UINT32(0, offset);
        ARG_CHECK_UINT32(1, rows);
    } else {
        ARG_CHECK_UINT32(0, rows);
    }

    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(args.This().As<Object>()));
    assert(query);

    query->sql << " LIMIT ";
    if (args.Length() > 1) {
        query->sql << args[0].ToNumber().Int32Value();
        query->sql << ",";
        query->sql << args[1].ToNumber().Int32Value();
    } else {
        query->sql << args[0].ToNumber().Int32Value();
    }

    return scope.Escape(args.This());
}

Napi::Value node_db::Query::Add(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);

    node_db::Query* innerQuery = NULL;

    if (args.Length() > 0 && args[0].IsObject()) {
        Napi::Object object = args[0].ToObject();
        String key = String::New(env, "sql");
        if (!object.Has(key) || !object.Get(key).IsFunction()) {
            ARG_CHECK_STRING(0, sql);
        }

        innerQuery = reinterpret_cast<node_db::Query*>(Unwrap(object));
        assert(innerQuery);
    } else {
        ARG_CHECK_STRING(0, sql);
    }

    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(args.This().As<Object>()));
    assert(query);

    if (innerQuery != NULL) {
        query->sql << innerQuery->sql.str();
    } else {
        Napi::String sql(env, args[0].ToString());
        query->sql << sql;
    }

    return scope.Escape(args.This());
}

Napi::Value node_db::Query::Delete(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);

    if (args.Length() > 0) {
        if (args[0].IsArray()) {
            ARG_CHECK_ARRAY(0, tables);
        } else if (args[0].IsObject()) {
            ARG_CHECK_OBJECT(0, tables);
        } else {
            ARG_CHECK_STRING(0, tables);
        }
        ARG_CHECK_OPTIONAL_BOOL(1, escape);
    }

    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(args.This().As<Object>()));
    assert(query);

    bool escape = true;
    if (args.Length() > 1) {
        escape = args[1].ToBoolean();
    }

    query->sql << "DELETE";

    if (args.Length() > 0) {
        try {
	  query->sql << " " << query->tableName(env, args[0], escape);
        } catch(const node_db::Exception& exception) {
            THROW_EXCEPTION(exception.what());
        }
    }

    return scope.Escape(args.This());
}

Napi::Value node_db::Query::Insert(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);
    uint32_t argsLength = args.Length();

    int fieldsIndex = -1, valuesIndex = -1;

    if (argsLength > 0) {
        ARG_CHECK_STRING(0, table);

        if (argsLength > 2) {
            if (args[1].IsArray()) {
                ARG_CHECK_ARRAY(1, fields);
            } else if (args[1].IsObject()) {
                ARG_CHECK_OBJECT(1, fields);
            } else if (args[1].ToBoolean().Value()) {
                ARG_CHECK_STRING(1, fields);
            }
            fieldsIndex = 1;

            if (args[2].ToBoolean().Value()) {
                valuesIndex = 2;
                ARG_CHECK_ARRAY(2, values);
            }

            ARG_CHECK_OPTIONAL_BOOL(3, escape);
        } else if (argsLength > 1) {
            ARG_CHECK_ARRAY(1, values);
            valuesIndex = 1;
        }
    } else {
        ARG_CHECK_STRING(0, table);
    }

    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(args.This().As<Object>()));
    assert(query);

    bool escape = true;
    if (argsLength > 3) {
        escape = args[3].ToBoolean();
    }

    try {
      query->sql << "INSERT INTO " << query->tableName(env, args[0], escape);
    } catch(const node_db::Exception& exception) {
        THROW_EXCEPTION(exception.what());
    }

    if (argsLength > 1) {
        if (fieldsIndex != -1) {
            query->sql << "(";
            if (args[fieldsIndex].IsArray()) {
                Napi::Array fields = args[fieldsIndex].As<Napi::Array>();
                if (fields.Length() == 0) {
                    THROW_EXCEPTION("No fields specified in insert")
                }

                for (uint32_t i = 0, limiti = fields.Length(); i < limiti; i++) {
                    if (i > 0) {
                        query->sql << ",";
                    }

                    try {
		        Napi::String fieldName(env, fields.Get(i));
                        //query->fieldName(env, fields.Get(i));
		        query->sql << fieldName;
                    } catch(const node_db::Exception& exception) {
                        THROW_EXCEPTION(exception.what())
                    }
                }
            } else {
                Napi::String fields(env, args[fieldsIndex].ToString());
                query->sql << fields;
            }
            query->sql << ")";
        }

        query->sql << " ";

        if (valuesIndex != -1) {
            Napi::Array values = args[valuesIndex].As<Napi::Array>();
            uint32_t valuesLength = values.Length();
            if (valuesLength > 0) {
	      bool multipleRecords = values.Get((uint32_t)0).IsArray();

                query->sql << "VALUES ";
                if (!multipleRecords) {
                    query->sql << "(";
                }

                for (uint32_t i = 0; i < valuesLength; i++) {
                    if (i > 0) {
                        query->sql << ",";
                    }
                    query->sql << query->value(values.Get(i));
                }

                if (!multipleRecords) {
                    query->sql << ")";
                }
            }
        }
    } else {
        query->sql << " ";
    }

    return scope.Escape(args.This());
}

Napi::Value node_db::Query::Update(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);

    if (args.Length() > 0) {
        if (args[0].IsArray()) {
            ARG_CHECK_ARRAY(0, tables);
        } else if (args[0].IsObject()) {
            ARG_CHECK_OBJECT(0, tables);
        } else {
            ARG_CHECK_STRING(0, tables);
        }
    } else {
        ARG_CHECK_STRING(0, tables);
    }

    ARG_CHECK_OPTIONAL_BOOL(1, escape);

    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(args.This().As<Object>()));
    assert(query);

    bool escape = true;
    if (args.Length() > 1) {
        escape = args[1].ToBoolean();
    }

    query->sql << "UPDATE ";

    try {
      query->sql << query->tableName(env, args[0], escape);
    } catch(const node_db::Exception& exception) {
        THROW_EXCEPTION(exception.what());
    }

    return scope.Escape(args.This());
}

Napi::Value node_db::Query::Set(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);

    ARG_CHECK_OBJECT(0, values);
    ARG_CHECK_OPTIONAL_BOOL(1, escape);

    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(args.This().As<Object>()));
    assert(query);

    bool escape = true;
    if (args.Length() > 1) {
        escape = args[1].ToBoolean();
    }

    query->sql << " SET ";

    Napi::Object values = args[0].ToObject();
    Napi::Array valueProperties = values.GetPropertyNames();
    if (valueProperties.Length() == 0) {
        THROW_EXCEPTION("Non empty objects should be used for values in set");
    }

    for (uint32_t j = 0, limitj = valueProperties.Length(); j < limitj; j++) {
        Napi::Value propertyName = valueProperties.Get(j);
        Napi::String fieldName(env, propertyName);
        Napi::Value currentValue = values.Get(propertyName);

        if (j > 0) {
            query->sql << ",";
        }

        query->sql << (escape ? query->connection->escapeName(fieldName) : fieldName);
        query->sql << "=";
        query->sql << query->value(currentValue);
    }

    return scope.Escape(args.This());
}

Napi::Value node_db::Query::Sql(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);

    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(args.This().As<Object>()));
    assert(query);

    return scope.Escape(String::New(env, query->sql.str().c_str()));
}

Napi::Value node_db::Query::Execute(const CallbackInfo& args) {
  Napi::Env env = args.Env();
  EscapableHandleScope scope(env);

    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(args.This().As<Object>()));
    assert(query);

    if (args.Length() > 0) {
        Napi::Value set = query->set(args);
        if (!set.IsEmpty()) {
            return scope.Escape(set);
        }
    }

    std::string sql;

    try {
        sql = query->parseQuery();
    } catch(const node_db::Exception& exception) {
        THROW_EXCEPTION(exception.what())
    }

    if (!query->cbStart.IsEmpty()) {
        Napi::Value argv[1];
        argv[0] = String::New(env, sql.c_str());

        Napi::Value result;
	try {
	  result = query->cbStart.Call(env.Global(), {argv[0]});
	} catch (const Napi::Error& e) {
	  e.Fatal("node_db::Query::Execute", "Error in start callback");
	}

        if (!result.IsUndefined()) {
            if (!result.ToBoolean()) {
                return scope.Escape(env.Undefined());
            } else if (result.IsString()) {
                Napi::String modifiedQuery(env, result.ToString());
                sql = modifiedQuery;
            }
        }
    }

    if (!query->connection->isAlive(false)) {
        THROW_EXCEPTION("Can't execute a query without being connected")
    }

    execute_request_t *request = new execute_request_t();
    if (request == NULL) {
        THROW_EXCEPTION("Could not create EIO request")
    }

    query->sql.str("");
    query->sql.clear();
    query->sql << sql;

    request->context = ObjectReference::New(args.This().As<Object>());
    request->query = query;
    request->buffered = false;
    request->result = NULL;
    request->rows = NULL;
    request->error = NULL;

    if (query->async) {
        request->query->Ref();

        uv_work_t* req = new uv_work_t();
        req->data = request;
        uv_queue_work(uv_default_loop(), req, uvExecute, (uv_after_work_cb)uvExecuteFinished);

        uv_ref((uv_handle_t *)&g_async);

    } else {
        request->query->executeAsync(request);
    }

    return scope.Escape(env.Undefined());
}

void node_db::Query::uvExecute(uv_work_t* uvRequest) {
    execute_request_t *request = static_cast<execute_request_t *>(uvRequest->data);
    assert(request);

    try {
        request->query->connection->lock();
        request->result = request->query->execute();
        request->query->connection->unlock();

        if (!request->result->isEmpty() && request->result != NULL) {
            request->rows = new std::vector<row_t*>();
            if (request->rows == NULL) {
                throw node_db::Exception("Could not create buffer for rows");
            }

            request->buffered = request->result->isBuffered();
            request->columnCount = request->result->columnCount();
            while (request->result->hasNext()) {
                unsigned long* columnLengths = request->result->columnLengths();
                char** currentRow = request->result->next();

                row_t* row = new row_t();
                if (row == NULL) {
                    throw node_db::Exception("Could not create buffer for row");
                }

                row->columnLengths = new unsigned long[request->columnCount];
                if (row->columnLengths == NULL) {
                    throw node_db::Exception("Could not create buffer for column lengths");
                }

                if (request->buffered) {
                    row->columns = currentRow;

                    for (uint16_t i = 0; i < request->columnCount; i++) {
                        row->columnLengths[i] = columnLengths[i];
                    }
                } else {
                    row->columns = new char*[request->columnCount];
                    if (row->columns == NULL) {
                        throw node_db::Exception("Could not create buffer for columns");
                    }

                    for (uint16_t i = 0; i < request->columnCount; i++) {
                        row->columnLengths[i] = columnLengths[i];
                        if (currentRow[i] != NULL) {
                            row->columns[i] = new char[row->columnLengths[i]];
                            if (row->columns[i] == NULL) {
                                throw node_db::Exception("Could not create buffer for column");
                            }
                            memcpy(row->columns[i], currentRow[i], row->columnLengths[i]);
                        } else {
                            row->columns[i] = NULL;
                        }
                    }
                }

                request->rows->push_back(row);
            }

            if (!request->result->isBuffered()) {
                request->result->release();
            }
        }
    } catch(const node_db::Exception& exception) {
        request->query->connection->unlock();
        Query::freeRequest(request, false);
        request->error = new std::string(exception.what());
    }
}

void node_db::Query::uvExecuteFinished(uv_work_t* uvRequest, int status) {
    execute_request_t *request = static_cast<execute_request_t *>(uvRequest->data);
    assert(request);

    Napi::Env env = request->context.Env();
    EscapableHandleScope scope(env);

    if (request->error == NULL && request->result != NULL) {
        Napi::Value argv[3];
        argv[0] = env.Null();

        bool isEmpty = request->result->isEmpty();
        if (!isEmpty) {
            assert(request->rows);

            size_t totalRows = request->rows->size();
            Napi::Array rows = Array::New(env, totalRows);

            uint64_t index = 0;
            std::ostringstream reusableStream;
            for (std::vector<row_t*>::iterator iterator = request->rows->begin(), end = request->rows->end(); iterator != end; ++iterator, index++) {
                row_t* currentRow = *iterator;
                Napi::Object row = request->query->row(env, request->result, currentRow);
                Napi::Value eachArgv[3];

                eachArgv[0] = row;
                eachArgv[1] = v8StringFromUInt64(env, index, reusableStream);
                eachArgv[2] = Boolean::New(env, (index == totalRows - 1));

                request->query->Emit("each", 3, eachArgv);

                rows.Set(index, row);
            }

            Napi::Array columns = Array::New(env, request->columnCount);
            for (uint16_t j = 0; j < request->columnCount; j++) {
                node_db::Result::Column *currentColumn = request->result->column(j);

                Napi::Object column = Object::New(env);
                column.Set("name", String::New(env, currentColumn->getName().c_str()));
                column.Set("type", NODE_CONSTANT(currentColumn->getType()));

                columns.Set(j, column);
            }

            argv[1] = rows;
            argv[2] = columns;
        } else {
            Napi::Object result = Object::New(env);
            std::ostringstream reusableStream;
            result.Set("id", v8StringFromUInt64(env, request->result->insertId(), reusableStream));
            result.Set("affected", v8StringFromUInt64(env, request->result->affectedCount(), reusableStream));
            result.Set("warning", v8StringFromUInt64(env, request->result->warningCount(), reusableStream));
            argv[1] = result;
        }

        request->query->Emit("success", !isEmpty ? 2 : 1, &argv[1]);

        if (!request->query->cbExecute.IsEmpty()) {
	  try {
	    if (isEmpty) {
	      request->query->cbExecute.Call(request->context.Value(), {argv[0], argv[1]});
	    } else {
	      request->query->cbExecute.Call(request->context.Value(), {argv[0], argv[1], argv[2]});
	    }
	  } catch (const Napi::Error& e) {
	    e.Fatal("node_db::Query::uvExecuteFinished", "Error in execute callback");
	  }
        }
    } else {
        Napi::Value argv[1];
        argv[0] = String::New(env, request->error != NULL ? request->error->c_str() : "(unknown error)");

        request->query->Emit("error", 1, argv);

        if (!request->query->cbExecute.IsEmpty()) {
	  try {
            request->query->cbExecute.Call(request->context.Value(), {argv[0]});
	  } catch (const Napi::Error& e) {
	    e.Fatal("node_db::Query::uvExecuteFinished", "Error in execute callback");
	  }
	}
    }

    if (!request->query->cbFinish.IsEmpty()) {
      try {
        request->query->cbFinish.Call(env.Global(), {});
      } catch (const Napi::Error& e) {
	e.Fatal("node_db::Query::uvExecuteFinished", "Error in finish callback");
      }
    }

    uv_unref((uv_handle_t *)&g_async);

    request->query->Unref();

    Query::freeRequest(request);
}

void node_db::Query::executeAsync(execute_request_t* request) {
    Napi::Env env = request->context.Env();
    bool freeAll = true;
    try {
        this->connection->lock();
        request->result = this->execute();
        this->connection->unlock();

        if (request->result != NULL) {
            Napi::Value argv[3];
            argv[0] = env.Null();

            bool isEmpty = request->result->isEmpty();
            if (!isEmpty) {
                request->columnCount = request->result->columnCount();

                Napi::Array columns = Array::New(env, request->columnCount);
                Napi::Array rows;
                try {
		  rows = Array::New(env, request->result->count());
                } catch(const node_db::Exception& exception) {
		  rows = Array::New(env);
                }

                for (uint16_t i = 0; i < request->columnCount; i++) {
                    node_db::Result::Column *currentColumn = request->result->column(i);

                    Napi::Object column = Object::New(env);
                    column.Set("name", String::New(env, currentColumn->getName().c_str()));
                    column.Set("type", NODE_CONSTANT(currentColumn->getType()));

                    columns.Set(i, column);
                }

                row_t row;
                uint64_t index = 0;
                std::ostringstream reusableStream;

                while (request->result->hasNext()) {
                    row.columnLengths = (unsigned long*) request->result->columnLengths();
                    row.columns = reinterpret_cast<char**>(request->result->next());

                    Napi::Object jsRow = this->row(env, request->result, &row);
                    Napi::Value eachArgv[3];

                    eachArgv[0] = jsRow;
                    eachArgv[1] = v8StringFromUInt64(env, index, reusableStream);
                    eachArgv[2] = Boolean::New(env, request->result->hasNext());

                    this->Emit("each", 3, eachArgv);

                    rows.Set(index++, jsRow);
                }

                if (!request->result->isBuffered()) {
                    request->result->release();
                }

                argv[1] = rows;
                argv[2] = columns;
            } else {
                Napi::Object result = Object::New(env);
                std::ostringstream reusableStream;
                result.Set("id", v8StringFromUInt64(env, request->result->insertId(), reusableStream));
                result.Set("affected", v8StringFromUInt64(env, request->result->affectedCount(), reusableStream));
                result.Set("warning", v8StringFromUInt64(env, request->result->warningCount(), reusableStream));
                argv[1] = result;
            }

            this->Emit("success", !isEmpty ? 2 : 1, &argv[1]);

            if (!this->cbExecute.IsEmpty()) {
	      try {
		if (isEmpty) {
		  this->cbExecute.Call(request->context.Value(), {argv[0], argv[1]});
		} else {
		  this->cbExecute.Call(request->context.Value(), {argv[0], argv[1], argv[2]});
		}
	      } catch (const Napi::Error& e) {
		e.Fatal("node_db::Query::executeAsync", "Error in execute callback");
	      }
            }
        }
    } catch(const node_db::Exception& exception) {
        this->connection->unlock();

        Napi::Value argv[1];
        argv[0] = String::New(env, exception.what());

        this->Emit("error", 1, argv);

        if (!this->cbExecute.IsEmpty()) {
	  try {
	    this->cbExecute.Call(request->context.Value(), {argv[0]});
	  } catch (const Napi::Error& e) {
	    e.Fatal("node_db::Query::executeAsync", "Error in execute callback");
	  }
        }

        freeAll = false;
    }

    Query::freeRequest(request, freeAll);
}

node_db::Result* node_db::Query::execute() const throw(node_db::Exception&) {
    return this->connection->query(this->sql.str());
}

void node_db::Query::freeRequest(execute_request_t* request, bool freeAll) {
    if (request->rows != NULL) {
        for (std::vector<row_t*>::iterator iterator = request->rows->begin(), end = request->rows->end(); iterator != end; ++iterator) {
            row_t* row = *iterator;
            if (!request->buffered) {
                for (uint16_t i = 0; i < request->columnCount; i++) {
                    if (row->columns[i] != NULL) {
                        delete row->columns[i];
                    }
                }
                delete [] row->columns;
            }
            delete [] row->columnLengths;
            delete row;
        }

        delete request->rows;
    }

    if (request->error != NULL) {
        delete request->error;
    }

    if (freeAll) {
        if (request->result != NULL) {
            delete request->result;
        }

        request->context.Reset();

        delete request;
    }
}

Napi::Value node_db::Query::set(const CallbackInfo& args) {
  Napi::Env env = args.Env();

    if (args.Length() == 0) {
        return Napi::Value();
    }

    int queryIndex = -1, optionsIndex = -1, valuesIndex = -1, callbackIndex = -1;

    if (args.Length() > 3) {
        ARG_CHECK_STRING(0, query);
        ARG_CHECK_ARRAY(1, values);
        ARG_CHECK_FUNCTION(2, callback);
        ARG_CHECK_OBJECT(3, options);
        queryIndex = 0;
        valuesIndex = 1;
        callbackIndex = 2;
        optionsIndex = 3;
    } else if (args.Length() == 3) {
        ARG_CHECK_STRING(0, query);
        queryIndex = 0;
        if (args[2].IsFunction()) {
            ARG_CHECK_FUNCTION(2, callback);
            if (args[1].IsArray()) {
                ARG_CHECK_ARRAY(1, values);
                valuesIndex = 1;
            } else {
                ARG_CHECK_OBJECT(1, options);
                optionsIndex = 1;
            }
            callbackIndex = 2;
        } else {
            ARG_CHECK_STRING(0, query);
            ARG_CHECK_ARRAY(1, values);
            ARG_CHECK_OBJECT(2, options);
            valuesIndex = 1;
            optionsIndex = 2;
        }
    } else if (args.Length() == 2) {
        if (args[1].IsFunction()) {
            ARG_CHECK_FUNCTION(1, callback);
            callbackIndex = 1;
        } else if (args[1].IsArray()) {
            ARG_CHECK_ARRAY(1, values);
            valuesIndex = 1;
        } else {
            ARG_CHECK_OBJECT(1, options);
            optionsIndex = 1;
        }

        if (args[0].IsFunction() && callbackIndex == -1) {
            ARG_CHECK_FUNCTION(0, callback);
            callbackIndex = 0;
        } else {
            ARG_CHECK_STRING(0, query);
            queryIndex = 0;
        }
    } else if (args.Length() == 1) {
        if (args[0].IsString()) {
            ARG_CHECK_STRING(0, query);
            queryIndex = 0;
        } else if (args[0].IsFunction()) {
            ARG_CHECK_FUNCTION(0, callback);
            callbackIndex = 0;
        } else if (args[0].IsArray()) {
            ARG_CHECK_ARRAY(0, values);
            valuesIndex = 0;
        } else {
            ARG_CHECK_OBJECT(0, options);
            optionsIndex = 0;
        }
    }

    if (queryIndex >= 0) {
        Napi::String initialSql(env, args[queryIndex].ToString());
        this->sql.str("");
        this->sql.clear();
        this->sql << initialSql;
    }

    if (optionsIndex >= 0) {
        Napi::Object options = args[optionsIndex].ToObject();

        ARG_CHECK_OBJECT_ATTR_OPTIONAL_BOOL(options, async);
        ARG_CHECK_OBJECT_ATTR_OPTIONAL_BOOL(options, cast);
        ARG_CHECK_OBJECT_ATTR_OPTIONAL_BOOL(options, bufferText);
        ARG_CHECK_OBJECT_ATTR_OPTIONAL_FUNCTION(options, start);
        ARG_CHECK_OBJECT_ATTR_OPTIONAL_FUNCTION(options, finish);

        if (options.Has(async_key)) {
            this->async = options.Get(async_key).ToBoolean();
        }

        if (options.Has(cast_key)) {
            this->cast = options.Get(cast_key).ToBoolean();
        }

        if (options.Has(bufferText_key)) {
            this->bufferText = options.Get(bufferText_key).ToBoolean();
        }

        if (options.Has(start_key)) {
	  this->cbStart.Reset(options.Get(start_key).As<Napi::Function>());
        }

        if (options.Has(finish_key)) {
	  this->cbFinish.Reset(options.Get(finish_key).As<Napi::Function>());
        }
    }

    if (valuesIndex >= 0) {
        Napi::Array values = args[valuesIndex].As<Napi::Array>();
        for (uint32_t i = 0, limiti = values.Length(); i < limiti; i++) {
	  this->values.push_back(Persistent(values.Get(i)));
        }
    }

    if (callbackIndex >= 0) {
      this->cbExecute.Reset(args[callbackIndex].As<Napi::Function>());
    }

    return Napi::Value();
}

std::string node_db::Query::fieldName(const Napi::Env& env, Napi::Value value) const throw(node_db::Exception&) {
    std::string buffer;

    if (value.IsObject()) {
        Napi::Object valueObject = value.ToObject();
        Napi::Array valueProperties = valueObject.GetPropertyNames();
        if (valueProperties.Length() == 0) {
            throw node_db::Exception("Non empty objects should be used for value aliasing in select");
        }

        for (uint32_t j = 0, limitj = valueProperties.Length(); j < limitj; j++) {
            Napi::Value propertyName = valueProperties.Get(j);
            Napi::String fieldName(env, propertyName);

            Napi::Value currentValue = valueObject.Get(propertyName);
            if (currentValue.IsObject() && !currentValue.IsArray() && !currentValue.IsFunction()) {
                Napi::Object currentObject = currentValue.ToObject();
                Napi::String escapeKey = String::New(env, "escape");
                Napi::String valueKey = String::New(env, "value");
                Napi::String precisionKey = String::New(env, "precision");
                Napi::Value optionValue;
                bool escape = false;
                int precision = -1;

                if (!currentObject.Has(valueKey)) {
                    throw node_db::Exception("The \"value\" option for the select field object must be specified");
                }

                if (currentObject.Has(escapeKey)) {
                    optionValue = currentObject.Get(escapeKey);
                    if (!optionValue.IsBoolean()) {
                        throw node_db::Exception("Specify a valid boolean value for the \"escape\" option in the select field object");
                    }
                    escape = optionValue.ToBoolean();
                }

                if (currentObject.Has(precisionKey)) {
                    optionValue = currentObject.Get(precisionKey);
                    if (!optionValue.IsNumber() || optionValue.As<Napi::Number>().Int64Value() < 0) {
                        throw new node_db::Exception("Specify a number equal or greater than 0 for precision");
                    }
                    precision = optionValue.As<Napi::Number>().Int64Value();
                }

                if (j > 0) {
                    buffer += ',';
                }

                buffer += this->value(currentObject.Get(valueKey), false, escape, precision);
            } else {
                if (j > 0) {
                    buffer += ',';
                }

                buffer += this->value(currentValue, false, currentValue.IsString() ? false : true);
            }

            buffer += " AS ";
            buffer += this->connection->escapeName(fieldName);
        }
    } else if (value.IsString()) {
        Napi::String fieldName(env, value.ToString());
        buffer += this->connection->escapeName(fieldName);
    } else {
        throw node_db::Exception("Incorrect value type provided as field for select");
    }

    return buffer;
}

std::string node_db::Query::tableName(const Napi::Env& env, Napi::Value value, bool escape) const throw(node_db::Exception&) {
    std::string buffer;

    if (value.IsArray()) {
        Napi::Array tables = value.As<Napi::Array>();
        if (tables.Length() == 0) {
            throw node_db::Exception("No tables specified");
        }

        for (uint32_t i = 0, limiti = tables.Length(); i < limiti; i++) {
            if (i > 0) {
                buffer += ',';
            }

            buffer += this->tableName(env, tables.Get(i), escape);
        }
    } else if (value.IsObject()) {
        Napi::Object valueObject = value.ToObject();
        Napi::Array valueProperties = valueObject.GetPropertyNames();
        if (valueProperties.Length() == 0) {
            throw node_db::Exception("Non empty objects should be used for aliasing");
        }

        Napi::Value propertyName = valueProperties.Get((uint32_t)0);
        Napi::Value propertyValue = valueObject.Get(propertyName);

        if (!propertyName.IsString() || !propertyValue.IsString()) {
            throw node_db::Exception("Only strings are allowed for table / alias name");
        }

        Napi::String table(env, propertyValue);
        Napi::String alias(env, propertyName);

        buffer += (escape ? this->connection->escapeName(table) : table);
        buffer += " AS ";
        buffer += (escape ? this->connection->escapeName(alias) : alias);
    } else {
        Napi::String tables(env, value.ToString());

        buffer += (escape ? this->connection->escapeName(tables) : tables);
    }

    return buffer;
}

Napi::Value node_db::Query::addCondition(const CallbackInfo& args, const char* separator) {
  Napi::Env env = args.Env();

    ARG_CHECK_STRING(0, conditions);
    ARG_CHECK_OPTIONAL_ARRAY(1, values);

    Napi::String conditions(env, args[0].ToString());
    std::string currentConditions = conditions;
    if (args.Length() > 1) {
        Napi::Array currentValues = args[1].As<Napi::Array>();
        for (uint32_t i = 0, limiti = currentValues.Length(); i < limiti; i++) {
	  this->values.push_back(Persistent(currentValues.Get(i)));
        }
    }

    this->sql << " " << separator << " ";
    this->sql << currentConditions;

    return args.This();
}

Napi::Object node_db::Query::row(const Napi::Env& env, node_db::Result* result, row_t* currentRow) const {
    Napi::Object row = Object::New(env);

    for (uint16_t j = 0, limitj = result->columnCount(); j < limitj; j++) {
        node_db::Result::Column* currentColumn = result->column(j);
        Napi::Value value;

        if (currentRow->columns[j] != NULL) {
            const char* currentValue = currentRow->columns[j];
            unsigned long currentLength = currentRow->columnLengths[j];
            if (this->cast) {
                node_db::Result::Column::type_t columnType = currentColumn->getType();
                switch (columnType) {
                    case node_db::Result::Column::BOOL:
		      value = Boolean::New(env, currentValue == NULL || currentLength == 0 || currentValue[0] != '0');
                        break;
                    case node_db::Result::Column::INT:
		      value = String::New(env, currentValue, currentLength).ToNumber();
                        break;
                    case node_db::Result::Column::NUMBER:
		      value = String::New(env, currentValue, currentLength).ToNumber();
                        break;
                    case node_db::Result::Column::TIME:
                        {
                            int hour, min, sec;
                            sscanf(currentValue, "%d:%d:%d", &hour, &min, &sec);
			    value = env.Global().Get("Date").As<Napi::Function>().New({
				Number::New(env, static_cast<uint64_t>((hour*60*60 + min*60 + sec) * 1000))
				  });
                        }
                        break;
                    case node_db::Result::Column::DATE:
                    case node_db::Result::Column::DATETIME:
                        // Code largely inspired from https://github.com/Sannis/node-mysql-libmysqlclient
                        try {
                            int day = 0, month = 0, year = 0, hour = 0, min = 0, sec = 0;
                            time_t rawtime;
                            struct tm timeinfo;

                            if (columnType == node_db::Result::Column::DATETIME) {
                                sscanf(currentValue, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec);
                            } else {
                                sscanf(currentValue, "%d-%d-%d", &year, &month, &day);
                            }

                            time(&rawtime);
                            if (!localtime_r(&rawtime, &timeinfo)) {
                                throw node_db::Exception("Can't get local time");
                            }

                            if (!Query::gmtDeltaLoaded) {
                                int localHour, gmtHour, localMin, gmtMin;

                                localHour = timeinfo.tm_hour - (timeinfo.tm_isdst > 0 ? 1 : 0);
                                localMin = timeinfo.tm_min;

                                if (!gmtime_r(&rawtime, &timeinfo)) {
                                    throw node_db::Exception("Can't get GMT time");
                                }
                                gmtHour = timeinfo.tm_hour;
                                gmtMin = timeinfo.tm_min;

                                Query::gmtDelta = ((localHour - gmtHour) * 60 + (localMin - gmtMin)) * 60;
                                if (Query::gmtDelta <= -(12 * 60 * 60)) {
                                    Query::gmtDelta += 24 * 60 * 60;
                                } else if (Query::gmtDelta > (12 * 60 * 60)) {
                                    Query::gmtDelta -= 24 * 60 * 60;
                                }
                                Query::gmtDeltaLoaded = true;
                            }

                            timeinfo.tm_year = year - 1900;
                            timeinfo.tm_mon = month - 1;
                            timeinfo.tm_mday = day;
                            timeinfo.tm_hour = hour;
                            timeinfo.tm_min = min;
                            timeinfo.tm_sec = sec;

			    value = env.Global().Get("Date").As<Napi::Function>().New({
				Number::New(env, static_cast<double>(mktime(&timeinfo) + Query::gmtDelta) * 1000)
				  });
                        } catch(const node_db::Exception&) {
			  value = String::New(env, currentValue, currentLength);
                        }
                        break;
                    case node_db::Result::Column::SET:
                        {
                            Napi::Array values = Array::New(env);
                            std::istringstream stream(currentValue);
                            std::string item;
                            uint64_t index = 0;
                            std::ostringstream reusableStream;
                            while (std::getline(stream, item, ',')) {
                                if (!item.empty()) {
				  values.Set(v8StringFromUInt64(env, index++, reusableStream), String::New(env, item.c_str()));
                                }
                            }
                            value = values;
                        }
                        break;
                    case node_db::Result::Column::TEXT:
                        if (this->bufferText || currentColumn->isBinary()) {
			  value = Buffer<char>::Copy(env, currentValue, currentLength);
                        } else {
			  value = String::New(env, currentValue, currentLength);
                        }
                        break;
                    default:
		      value = String::New(env, currentValue, currentLength);
                        break;
                }
            } else {
	      value = String::New(env, currentValue, currentLength);
            }
        } else {
            value = env.Null();
        }
        row.Set(String::New(env, currentColumn->getName().c_str()), value);
    }

    return row;
}

std::vector<std::string::size_type> node_db::Query::placeholders(std::string* parsed) const throw(node_db::Exception&) {
    std::string query = this->sql.str();
    std::vector<std::string::size_type> positions;
    char quote = 0;
    bool escaped = false;
    uint32_t delta = 0;

    *parsed = query;

    for (std::string::size_type i = 0, limiti = query.length(); i < limiti; i++) {
        char currentChar = query[i];
        if (escaped) {
            if (currentChar == '?') {
                parsed->replace(i - 1 - delta, 1, "");
                delta++;
            }
            escaped = false;
        } else if (currentChar == '\\') {
            escaped = true;
        } else if (quote && currentChar == quote) {
            quote = 0;
        } else if (!quote && (currentChar == this->connection->quoteString)) {
            quote = currentChar;
        } else if (!quote && currentChar == '?') {
            positions.push_back(i - delta);
        }
    }

    if (positions.size() != this->values.size()) {
        throw node_db::Exception("Wrong number of values to escape");
    }

    return positions;
}

std::string node_db::Query::parseQuery() const throw(node_db::Exception&) {
    std::string parsed;
    std::vector<std::string::size_type> positions = this->placeholders(&parsed);

    uint32_t index = 0, delta = 0;
    for (std::vector<std::string::size_type>::iterator iterator = positions.begin(), end = positions.end(); iterator != end; ++iterator, index++) {
      std::string value = this->value(this->values[index].Value());

	if(!value.length()) {
		throw node_db::Exception("Internal error, attempting to replace with zero length value");
	}

        parsed.replace(*iterator + delta, 1, value);
        delta += (value.length() - 1);
    }

    return parsed;
}

std::string node_db::Query::value(Napi::Value value, bool inArray, bool escape, int precision) const throw(node_db::Exception&) {
  Napi::Env env = value.Env();
    std::ostringstream currentStream;

    if (value.IsNull()) {
        currentStream << "NULL";
    } else if (value.IsArray()) {
        Napi::Array array = value.As<Napi::Array>();
        if (!inArray) {
            currentStream << '(';
        }
        for (uint32_t i = 0, limiti = array.Length(); i < limiti; i++) {
            Napi::Value child = array.Get(i);
            if (child.IsArray() && i > 0) {
                currentStream << "),(";
            } else if (i > 0) {
                currentStream << ',';
            }

            currentStream << this->value(child, true, escape);
        }
        if (!inArray) {
            currentStream << ')';
        }
    } else if (value.IsObject()) {
      if (value.ToObject().InstanceOf(env.Global().Get("Date").As<Napi::Function>())) {
        currentStream << this->connection->quoteString << this->fromDate(value.ToNumber().DoubleValue()) << this->connection->quoteString;
      } else {
        Napi::Object object = value.ToObject();
        String valueKey = String::New(env, "value");
        String escapeKey = String::New(env, "escape");

        if (object.Has(valueKey)) {
	  String precisionKey = String::New(env, "precision");
            int precision = -1;

            if (object.Has(precisionKey)) {
                Napi::Value optionValue = object.Get(precisionKey);
                if (!optionValue.IsNumber() || optionValue.As<Napi::Number>().Int64Value() < 0) {
                    throw new node_db::Exception("Specify a number equal or greater than 0 for precision");
                }
                precision = optionValue.As<Napi::Number>().Int64Value();
            }

            bool innerEscape = true;
            if (object.Has(escapeKey)) {
                Napi::Value escapeValue = object.Get(escapeKey);
                if (!escapeValue.IsBoolean()) {
                    throw node_db::Exception("Specify a valid boolean value for the \"escape\" option in the select field object");
                }
                innerEscape = escapeValue.ToBoolean();
            }
            currentStream << this->value(object.Get(valueKey), false, innerEscape, precision);
        } else {
	  String sqlKey = String::New(env, "sql");
            if (!object.Has(sqlKey) || !object.Get(sqlKey).IsFunction()) {
                throw node_db::Exception("Objects can't be converted to a SQL value");
            }

	    node_db::Query* query = reinterpret_cast<node_db::Query*>(Unwrap(object));
            assert(query);
            if (escape) {
                currentStream << "(";
            }
            currentStream << query->sql.str();
            if (escape) {
                currentStream << ")";
            }
        }
      }
    } else if (value.IsBoolean()) {
        currentStream << (value.ToBoolean() ? '1' : '0');
    } else if (value.IsNumber() && value.As<Napi::Number>().DoubleValue() == value.As<Napi::Number>().Int64Value()) {
        currentStream << value.As<Napi::Number>().Int64Value();
    } else if (value.IsNumber()) {
        if (precision == -1) {
            Napi::String currentString(env, value.ToString());
            currentStream << currentString;
        } else {
            currentStream << std::fixed << std::setprecision(precision) << value.As<Napi::Number>().DoubleValue();
        }
    } else if (value.IsString()) {
        Napi::String currentString(env, value.ToString());
        std::string string = currentString;
        if (escape) {
            try {
                currentStream << this->connection->quoteString << this->connection->escape(string) << this->connection->quoteString;
            } catch(node_db::Exception& exception) {
                currentStream << this->connection->quoteString << string << this->connection->quoteString;
            }
        } else {
            currentStream << string;
        }
    } else {
        Napi::String currentString(env, value.ToString());
        std::string string = currentString;
        throw node_db::Exception("Unknown type for to convert to SQL, converting `" + string + "'");
    }

    return currentStream.str();
}

std::string node_db::Query::fromDate(const double timeStamp) const throw(node_db::Exception&) {
    char* buffer = new char[20];
    if (buffer == NULL) {
        throw node_db::Exception("Can\'t create buffer to write parsed date");
    }


    struct tm timeinfo;
    time_t rawtime = (time_t) (timeStamp / 1000);
    if (!localtime_r(&rawtime, &timeinfo)) {
        throw node_db::Exception("Can't get local time");
    }

    strftime(buffer, 20, "%Y-%m-%d %H:%M:%S", &timeinfo);

    std::string date(buffer);
    delete [] buffer;

    return date;
}

