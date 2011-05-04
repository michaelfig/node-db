// Copyright 2011 Mariano Iglesias <mgiglesias@gmail.com>
#include "./query.h"

v8::Persistent<v8::String> node_db::Query::sySuccess;
v8::Persistent<v8::String> node_db::Query::syError;
v8::Persistent<v8::String> node_db::Query::syEach;
bool node_db::Query::gmtDeltaLoaded = false;
int node_db::Query::gmtDelta;

void node_db::Query::Init(v8::Handle<v8::Object> target, v8::Persistent<v8::FunctionTemplate> constructorTemplate) {
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "select", Select);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "from", From);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "join", Join);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "where", Where);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "and", And);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "or", Or);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "limit", Limit);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "add", Add);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "insert", Insert);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "update", Update);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "set", Set);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "delete", Delete);
    NODE_ADD_PROTOTYPE_METHOD(constructorTemplate, "execute", Execute);

    sySuccess = NODE_PERSISTENT_SYMBOL("success");
    syError = NODE_PERSISTENT_SYMBOL("error");
    syEach = NODE_PERSISTENT_SYMBOL("each");
}

node_db::Query::Query(): node::EventEmitter(),
    connection(NULL), async(true), cast(true), bufferText(false), cbStart(NULL), cbSuccess(NULL), cbFinish(NULL) {
}

node_db::Query::~Query() {
    this->values.Dispose();
    if (this->cbStart != NULL) {
        node::cb_destroy(this->cbStart);
    }
    if (this->cbSuccess != NULL) {
        node::cb_destroy(this->cbSuccess);
    }
    if (this->cbFinish != NULL) {
        node::cb_destroy(this->cbFinish);
    }
}

void node_db::Query::setConnection(node_db::Connection* connection) {
    this->connection = connection;
}

v8::Handle<v8::Value> node_db::Query::Select(const v8::Arguments& args) {
    v8::HandleScope scope;

    if (args.Length() > 0) {
        if (args[0]->IsArray()) {
            ARG_CHECK_ARRAY(0, fields);
        } else if (args[0]->IsObject()) {
            ARG_CHECK_OBJECT(0, fields);
        } else {
            ARG_CHECK_STRING(0, fields);
        }
    } else {
        ARG_CHECK_STRING(0, fields);
    }

    node_db::Query *query = node::ObjectWrap::Unwrap<node_db::Query>(args.This());
    assert(query);

    query->sql << "SELECT ";

    if (args[0]->IsArray()) {
        v8::Local<v8::Array> fields = v8::Array::Cast(*args[0]);
        if (fields->Length() == 0) {
            THROW_EXCEPTION("No fields specified in select")
        }

        for (uint32_t i = 0, limiti = fields->Length(); i < limiti; i++) {
            if (i > 0) {
                query->sql << ",";
            }

            try {
                query->sql << query->fieldName(fields->Get(i));
            } catch(const node_db::Exception& exception) {
                THROW_EXCEPTION(exception.what())
            }
        }
    } else if (args[0]->IsObject()) {
        try {
            query->sql << query->fieldName(args[0]);
        } catch(const node_db::Exception& exception) {
            THROW_EXCEPTION(exception.what())
        }
    } else {
        v8::String::Utf8Value fields(args[0]->ToString());
        query->sql << *fields;
    }

    return scope.Close(args.This());
}

v8::Handle<v8::Value> node_db::Query::From(const v8::Arguments& args) {
    v8::HandleScope scope;

    if (args.Length() > 0) {
        if (args[0]->IsObject()) {
            ARG_CHECK_OBJECT(0, tables);
        } else {
            ARG_CHECK_STRING(0, tables);
        }
    } else {
        ARG_CHECK_STRING(0, tables);
    }

    ARG_CHECK_OPTIONAL_BOOL(1, escape);

    node_db::Query *query = node::ObjectWrap::Unwrap<node_db::Query>(args.This());
    assert(query);

    bool escape = true;
    if (args.Length() > 1) {
        escape = args[1]->IsTrue();
    }

    query->sql << " FROM ";

    try {
        query->sql << query->tableName(args[0], escape);
    } catch(const node_db::Exception& exception) {
        THROW_EXCEPTION(exception.what());
    }

    return scope.Close(args.This());
}

v8::Handle<v8::Value> node_db::Query::Join(const v8::Arguments& args) {
    v8::HandleScope scope;

    ARG_CHECK_OBJECT(0, join);
    ARG_CHECK_OPTIONAL_ARRAY(1, values);

    v8::Local<v8::Object> join = args[0]->ToObject();

    ARG_CHECK_OBJECT_ATTR_OPTIONAL_STRING(join, type);
    ARG_CHECK_OBJECT_ATTR_STRING(join, table);
    ARG_CHECK_OBJECT_ATTR_OPTIONAL_STRING(join, alias);
    ARG_CHECK_OBJECT_ATTR_OPTIONAL_STRING(join, conditions);
    ARG_CHECK_OBJECT_ATTR_OPTIONAL_BOOL(join, escape);

    node_db::Query *query = node::ObjectWrap::Unwrap<node_db::Query>(args.This());
    assert(query);

    std::string type = "INNER";
    bool escape = true;

    if (join->Has(type_key)) {
        v8::String::Utf8Value currentType(join->Get(type_key)->ToString());
        type = *currentType;
        std::transform(type.begin(), type.end(), type.begin(), toupper);
    }

    if (join->Has(escape_key)) {
        escape = join->Get(escape_key)->IsTrue();
    }

    v8::String::Utf8Value table(join->Get(table_key)->ToString());

    query->sql << " " << type << " JOIN ";
    if (escape) {
        query->sql << query->connection->quoteTable;
    }
    query->sql << *table;
    if (escape) {
        query->sql << query->connection->quoteTable;
    }

    if (join->Has(alias_key)) {
        v8::String::Utf8Value alias(join->Get(alias_key)->ToString());

        query->sql << " AS ";
        if (escape) {
            query->sql << query->connection->quoteTable;
        }
        query->sql << *alias;
        if (escape) {
            query->sql << query->connection->quoteTable;
        }
    }

    if (join->Has(conditions_key)) {
        v8::String::Utf8Value conditions(join->Get(conditions_key)->ToObject());
        std::string currentConditions = *conditions;
        v8::Local<v8::Array> currentValues;
        if (args.Length() > 1) {
            currentValues = v8::Array::Cast(*args[1]);
        }

        try {
            currentConditions = query->parseQuery(currentConditions, *currentValues);
        } catch(const node_db::Exception& exception) {
            THROW_EXCEPTION(exception.what())
        }

        query->sql << " ON (" << currentConditions << ")";
    }

    return scope.Close(args.This());
}

v8::Handle<v8::Value> node_db::Query::Where(const v8::Arguments& args) {
    v8::HandleScope scope;

    node_db::Query *query = node::ObjectWrap::Unwrap<node_db::Query>(args.This());
    assert(query);

    return scope.Close(query->addCondition(args, "WHERE"));
}

v8::Handle<v8::Value> node_db::Query::And(const v8::Arguments& args) {
    v8::HandleScope scope;

    node_db::Query *query = node::ObjectWrap::Unwrap<node_db::Query>(args.This());
    assert(query);

    return scope.Close(query->addCondition(args, "AND"));
}

v8::Handle<v8::Value> node_db::Query::Or(const v8::Arguments& args) {
    v8::HandleScope scope;

    node_db::Query *query = node::ObjectWrap::Unwrap<node_db::Query>(args.This());
    assert(query);

    return scope.Close(query->addCondition(args, "OR"));
}

v8::Handle<v8::Value> node_db::Query::Limit(const v8::Arguments& args) {
    v8::HandleScope scope;

    if (args.Length() > 1) {
        ARG_CHECK_UINT32(0, offset);
        ARG_CHECK_UINT32(1, rows);
    } else {
        ARG_CHECK_UINT32(0, rows);
    }

    node_db::Query *query = node::ObjectWrap::Unwrap<node_db::Query>(args.This());
    assert(query);

    query->sql << " LIMIT ";
    if (args.Length() > 1) {
        query->sql << args[0]->ToInt32()->Value();
        query->sql << ",";
        query->sql << args[1]->ToInt32()->Value();
    } else {
        query->sql << args[0]->ToInt32()->Value();
    }

    return scope.Close(args.This());
}

v8::Handle<v8::Value> node_db::Query::Add(const v8::Arguments& args) {
    v8::HandleScope scope;

    ARG_CHECK_STRING(0, sql);

    node_db::Query *query = node::ObjectWrap::Unwrap<node_db::Query>(args.This());
    assert(query);

    v8::String::Utf8Value sql(args[0]->ToString());
    query->sql << " " << *sql;

    return scope.Close(args.This());
}

v8::Handle<v8::Value> node_db::Query::Delete(const v8::Arguments& args) {
    v8::HandleScope scope;

    if (args.Length() > 0) {
        if (args[0]->IsObject()) {
            ARG_CHECK_OBJECT(0, tables);
        } else {
            ARG_CHECK_STRING(0, tables);
        }
        ARG_CHECK_OPTIONAL_BOOL(1, escape);
    }

    node_db::Query *query = node::ObjectWrap::Unwrap<node_db::Query>(args.This());
    assert(query);

    bool escape = true;
    if (args.Length() > 1) {
        escape = args[1]->IsTrue();
    }

    query->sql << "DELETE";

    if (args.Length() > 0) {
        try {
            query->sql << " " << query->tableName(args[0], escape);
        } catch(const node_db::Exception& exception) {
            THROW_EXCEPTION(exception.what());
        }
    }

    return scope.Close(args.This());
}

v8::Handle<v8::Value> node_db::Query::Insert(const v8::Arguments& args) {
    v8::HandleScope scope;
    uint32_t argsLength = args.Length();

    if (argsLength > 0) {
        if (argsLength > 1) {
            if (args[1]->IsArray()) {
                ARG_CHECK_ARRAY(1, fields);
            } else if (args[1]->IsObject()) {
                ARG_CHECK_OBJECT(1, fields);
            } else if (!args[1]->IsFalse()) {
                ARG_CHECK_STRING(1, fields);
            }
        }

        if (argsLength > 2) {
            ARG_CHECK_ARRAY(2, values);
            ARG_CHECK_OPTIONAL_BOOL(3, escape);
        }
    } else {
        ARG_CHECK_STRING(0, table);
    }

    node_db::Query *query = node::ObjectWrap::Unwrap<node_db::Query>(args.This());
    assert(query);

    bool escape = true;
    if (argsLength > 3) {
        escape = args[3]->IsTrue();
    }

    try {
        query->sql << "INSERT INTO " << query->tableName(args[0], escape);
    } catch(const node_db::Exception& exception) {
        THROW_EXCEPTION(exception.what());
    }

    if (argsLength > 1) {
        if (!args[1]->IsFalse()) {
            query->sql << "(";
            if (args[1]->IsArray()) {
                v8::Local<v8::Array> fields = v8::Array::Cast(*args[1]);
                if (fields->Length() == 0) {
                    THROW_EXCEPTION("No fields specified in insert")
                }

                for (uint32_t i = 0, limiti = fields->Length(); i < limiti; i++) {
                    if (i > 0) {
                        query->sql << ",";
                    }

                    try {
                        query->sql << query->fieldName(fields->Get(i));
                    } catch(const node_db::Exception& exception) {
                        THROW_EXCEPTION(exception.what())
                    }
                }
            } else {
                v8::String::Utf8Value fields(args[1]->ToString());
                query->sql << *fields;
            }
            query->sql << ")";
        }

        query->sql << " ";

        if (argsLength > 2) {
            v8::Local<v8::Array> values = v8::Array::Cast(*args[2]);
            uint32_t valuesLength = values->Length();
            if (valuesLength > 0) {
                bool multipleRecords = values->Get(0)->IsArray();

                query->sql << "VALUES ";
                if (!multipleRecords) {
                    query->sql << "(";
                }

                for (uint32_t i=0; i < valuesLength; i++) {
                    if (i > 0) {
                        query->sql << ",";
                    }
                    query->sql << query->value(values->Get(i));
                }

                if (!multipleRecords) {
                    query->sql << ")";
                }
            }
        }
    } else {
        query->sql << " ";
    }

    return scope.Close(args.This());
}

v8::Handle<v8::Value> node_db::Query::Update(const v8::Arguments& args) {
    v8::HandleScope scope;

    if (args.Length() > 0) {
        if (args[0]->IsObject()) {
            ARG_CHECK_OBJECT(0, tables);
        } else {
            ARG_CHECK_STRING(0, tables);
        }
    } else {
        ARG_CHECK_STRING(0, tables);
    }

    ARG_CHECK_OPTIONAL_BOOL(1, escape);

    node_db::Query *query = node::ObjectWrap::Unwrap<node_db::Query>(args.This());
    assert(query);

    bool escape = true;
    if (args.Length() > 1) {
        escape = args[1]->IsTrue();
    }

    query->sql << "UPDATE ";

    try {
        query->sql << query->tableName(args[0], escape);
    } catch(const node_db::Exception& exception) {
        THROW_EXCEPTION(exception.what());
    }

    return scope.Close(args.This());
}

v8::Handle<v8::Value> node_db::Query::Set(const v8::Arguments& args) {
    v8::HandleScope scope;

    ARG_CHECK_OBJECT(0, values);
    ARG_CHECK_OPTIONAL_BOOL(1, escape);

    node_db::Query *query = node::ObjectWrap::Unwrap<node_db::Query>(args.This());
    assert(query);

    bool escape = true;
    if (args.Length() > 1) {
        escape = args[1]->IsTrue();
    }

    query->sql << " SET ";

    v8::Local<v8::Object> values = args[0]->ToObject();
    v8::Local<v8::Array> valueProperties = values->GetPropertyNames();
    if (valueProperties->Length() == 0) {
        THROW_EXCEPTION("Non empty objects should be used for values in set");
    }

    for (uint32_t j = 0, limitj = valueProperties->Length(); j < limitj; j++) {
        v8::Local<v8::Value> propertyName = valueProperties->Get(j);
        v8::String::Utf8Value fieldName(propertyName);
        v8::Local<v8::Value> currentValue = values->Get(propertyName);

        if (j > 0) {
            query->sql << ",";
        }

        if (escape) {
            query->sql << query->connection->quoteTable;
        }
        query->sql << *fieldName;
        if (escape) {
            query->sql << query->connection->quoteTable;
        }
        query->sql << "=";

        bool escape = true;
        if (currentValue->IsObject() && !currentValue->IsArray() && !currentValue->IsFunction() && !currentValue->IsDate()) {
            v8::Local<v8::Object> currentObject = currentValue->ToObject();
            v8::Local<v8::String> escapeKey = v8::String::New("escape");
            v8::Local<v8::String> valueKey = v8::String::New("value");
            v8::Local<v8::Value> optionValue;

            if (!currentObject->Has(valueKey)) {
                throw node_db::Exception("The \"value\" option for the select field object must be specified");
            }

            if (currentObject->Has(escapeKey)) {
                optionValue = currentObject->Get(escapeKey);
                if (!optionValue->IsBoolean()) {
                    throw node_db::Exception("Specify a valid boolean value for the \"escape\" option in the select field object");
                }
                escape = optionValue->IsTrue();
            }

            currentValue = currentObject->Get(valueKey);
        }

        query->sql << query->value(currentValue, false, escape);
    }

    return scope.Close(args.This());
}

v8::Handle<v8::Value> node_db::Query::Execute(const v8::Arguments& args) {
    v8::HandleScope scope;

    node_db::Query *query = node::ObjectWrap::Unwrap<node_db::Query>(args.This());
    assert(query);

    if (args.Length() > 0) {
        v8::Handle<v8::Value> set = query->set(args);
        if (!set.IsEmpty()) {
            return scope.Close(set);
        }
    }

    std::string sql = query->sql.str();

    try {
        sql = query->parseQuery(sql, *(query->values));
    } catch(const node_db::Exception& exception) {
        THROW_EXCEPTION(exception.what())
    }

    if (query->cbStart != NULL && !query->cbStart->IsEmpty()) {
        v8::Local<v8::Value> argv[1];
        argv[0] = v8::String::New(sql.c_str());

        v8::TryCatch tryCatch;
        v8::Handle<v8::Value> result = (*(query->cbStart))->Call(v8::Context::GetCurrent()->Global(), 1, argv);
        if (tryCatch.HasCaught()) {
            node::FatalException(tryCatch);
        }

        if (!result->IsUndefined()) {
            if (result->IsFalse()) {
                return scope.Close(v8::Undefined());
            } else if (result->IsString()) {
                v8::String::Utf8Value modifiedQuery(result->ToString());
                sql = *modifiedQuery;
            }
        }
    }

    execute_request_t *request = new execute_request_t();
    if (request == NULL) {
        THROW_EXCEPTION("Could not create EIO request")
    }

    query->sql.str("");
    query->sql.clear();
    query->sql << sql;

    request->query = query;
    request->result = NULL;
    request->rows = NULL;
    request->error = NULL;

    if (query->async) {
        request->query->Ref();
        eio_custom(eioExecute, EIO_PRI_DEFAULT, eioExecuteFinished, request);
        ev_ref(EV_DEFAULT_UC);
    } else {
        request->query->execute(request);
        request->query->executeFinished(request);
    }

    return scope.Close(v8::Undefined());
}


std::string node_db::Query::fieldName(v8::Local<v8::Value> value) const throw(node_db::Exception&) {
    std::ostringstream buffer;

    if (value->IsObject()) {
        v8::Local<v8::Object> valueObject = value->ToObject();
        v8::Local<v8::Array> valueProperties = valueObject->GetPropertyNames();
        if (valueProperties->Length() == 0) {
            throw node_db::Exception("Non empty objects should be used for value aliasing in select");
        }

        for (uint32_t j = 0, limitj = valueProperties->Length(); j < limitj; j++) {
            v8::Local<v8::Value> propertyName = valueProperties->Get(j);
            v8::String::Utf8Value fieldName(propertyName);

            v8::Local<v8::Value> currentValue = valueObject->Get(propertyName);
            if (currentValue->IsObject() && !currentValue->IsArray() && !currentValue->IsFunction() && !currentValue->IsDate()) {
                v8::Local<v8::Object> currentObject = currentValue->ToObject();
                v8::Local<v8::String> escapeKey = v8::String::New("escape");
                v8::Local<v8::String> valueKey = v8::String::New("value");
                v8::Local<v8::Value> optionValue;
                bool escape = false;

                if (!currentObject->Has(valueKey)) {
                    throw node_db::Exception("The \"value\" option for the select field object must be specified");
                }

                if (currentObject->Has(escapeKey)) {
                    optionValue = currentObject->Get(escapeKey);
                    if (!optionValue->IsBoolean()) {
                        throw node_db::Exception("Specify a valid boolean value for the \"escape\" option in the select field object");
                    }
                    escape = optionValue->IsTrue();
                }

                if (j > 0) {
                    buffer << ",";
                }

                buffer << this->value(currentObject->Get(valueKey), false, escape);
            } else {
                if (j > 0) {
                    buffer << ",";
                }

                buffer << this->value(currentValue, false, currentValue->IsString() ? false : true);
            }

            buffer << " AS " << this->connection->quoteField << *fieldName << this->connection->quoteField;
        }
    } else if (value->IsString()) {
        v8::String::Utf8Value fieldName(value->ToString());
        buffer << this->connection->quoteField << *fieldName << this->connection->quoteField;
    } else {
        throw node_db::Exception("Incorrect value type provided as field for select");
    }

    return buffer.str();
}

std::string node_db::Query::tableName(v8::Local<v8::Value> value, bool escape) const throw(node_db::Exception&) {
    std::ostringstream buffer;

    if (value->IsObject()) {
        v8::Local<v8::Object> valueObject = value->ToObject();
        v8::Local<v8::Array> valueProperties = valueObject->GetPropertyNames();
        if (valueProperties->Length() == 0) {
            throw node_db::Exception("Non empty objects should be used for aliasing");
        }

        v8::Local<v8::Value> propertyName = valueProperties->Get(0);
        v8::Local<v8::Value> propertyValue = valueObject->Get(propertyName);

        if (!propertyName->IsString() || !propertyValue->IsString()) {
            throw node_db::Exception("Only strings are allowed for table / alias name");
        }

        v8::String::Utf8Value table(propertyValue);
        v8::String::Utf8Value alias(propertyName);

        if (escape) {
            buffer << this->connection->quoteTable;
        }
        buffer << *table;
        if (escape) {
            buffer << this->connection->quoteTable;
        }
        buffer << " AS ";
        if (escape) {
            buffer << this->connection->quoteTable;
        }
        buffer << *alias;
        if (escape) {
            buffer << this->connection->quoteTable;
        }
    } else {
        v8::String::Utf8Value tables(value->ToString());

        if (escape) {
            buffer << this->connection->quoteTable;
        }
        buffer << *tables;
        if (escape) {
            buffer << this->connection->quoteTable;
        }
    }

    return buffer.str();
}

v8::Handle<v8::Value> node_db::Query::addCondition(const v8::Arguments& args, const char* separator) {
    ARG_CHECK_STRING(0, conditions);
    ARG_CHECK_OPTIONAL_ARRAY(1, values);

    v8::String::Utf8Value conditions(args[0]->ToString());
    std::string currentConditions = *conditions;
    v8::Local<v8::Array> currentValues;
    if (args.Length() > 1) {
        currentValues = v8::Array::Cast(*args[1]);
    }

    try {
        currentConditions = this->parseQuery(currentConditions, *currentValues);
    } catch(const node_db::Exception& exception) {
        THROW_EXCEPTION(exception.what())
    }

    this->sql << " " << separator << " ";
    this->sql << currentConditions;

    return args.This();
}

int node_db::Query::eioExecute(eio_req* eioRequest) {
    execute_request_t *request = static_cast<execute_request_t *>(eioRequest->data);
    assert(request);

    request->query->execute(request);

    return 0;
}

int node_db::Query::eioExecuteFinished(eio_req* eioRequest) {
    v8::HandleScope scope;

    execute_request_t *request = static_cast<execute_request_t *>(eioRequest->data);
    assert(request);

    request->query->executeFinished(request);

    return 0;
}

void node_db::Query::execute(execute_request_t* request) {
    try {
        request->result = this->connection->query(this->sql.str());
        if (request->result != NULL) {
            request->rows = new std::vector<row_t*>();
            if (request->rows == NULL) {
                throw node_db::Exception("Could not create buffer for rows");
            }

            request->columnCount = request->result->columnCount();
            while (request->result->hasNext()) {
                uint64_t *columnLengths = request->result->columnLengths();
                const char** currentRow = request->result->next();

                row_t* row = new row_t();
                if (row == NULL) {
                    throw node_db::Exception("Could not create buffer for row");
                }

                row->columnLengths = new uint64_t[request->columnCount];
                if (row->columnLengths == NULL) {
                    throw node_db::Exception("Could not create buffer for column lengths");
                }

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
                        strncpy(row->columns[i], currentRow[i], row->columnLengths[i]);
                    } else {
                        row->columns[i] = NULL;
                    }
                }

                request->rows->push_back(row);
            }
        }
    } catch(const node_db::Exception& exception) {
        Query::freeRequest(request, false);
        request->error = exception.what();
    }
}

void node_db::Query::executeFinished(execute_request_t* request) {
    uint16_t columnCount = (request->result != NULL ? request->result->columnCount() : 0);
    if (request->error == NULL && request->result != NULL) {
        assert(request->rows);

        v8::Local<v8::Array> rows = v8::Array::New(request->rows->size());

        uint64_t index = 0;
        for (std::vector<row_t*>::iterator iterator = request->rows->begin(), end = request->rows->end(); iterator != end; ++iterator, index++) {
            row_t* currentRow = *iterator;
            v8::Local<v8::Object> row = this->row(request->result, currentRow);
            v8::Local<v8::Value> eachArgv[3];

            eachArgv[0] = row;
            eachArgv[1] = v8::Number::New(index);
            eachArgv[2] = v8::Local<v8::Value>::New(iterator == end ? v8::True() : v8::False());

            this->Emit(syEach, 3, eachArgv);

            rows->Set(index, row);
        }

        v8::Local<v8::Array> columns = v8::Array::New(columnCount);
        for (uint16_t j = 0; j < columnCount; j++) {
            node_db::Result::Column *currentColumn = request->result->column(j);
            v8::Local<v8::Value> columnType;

            v8::Local<v8::Object> column = v8::Object::New();
            column->Set(v8::String::New("name"), v8::String::New(currentColumn->getName().c_str()));
            column->Set(v8::String::New("type"), NODE_CONSTANT(currentColumn->getType()));

            columns->Set(j, column);
        }

        v8::Local<v8::Value> argv[2];
        argv[0] = rows;
        argv[1] = columns;

        this->Emit(sySuccess, 2, argv);

        if (this->cbSuccess != NULL && !this->cbSuccess->IsEmpty()) {
            v8::TryCatch tryCatch;
            (*(this->cbSuccess))->Call(v8::Context::GetCurrent()->Global(), 2, argv);
            if (tryCatch.HasCaught()) {
                node::FatalException(tryCatch);
            }
        }
    } else {
        v8::Local<v8::Value> argv[1];
        argv[0] = v8::String::New(request->error != NULL ? request->error : "(unknown error)");

        this->Emit(syError, 1, argv);
    }

    if (this->cbFinish != NULL && !this->cbFinish->IsEmpty()) {
        v8::TryCatch tryCatch;
        (*(this->cbFinish))->Call(v8::Context::GetCurrent()->Global(), 0, NULL);
        if (tryCatch.HasCaught()) {
            node::FatalException(tryCatch);
        }
    }

    if (this->async) {
        ev_unref(EV_DEFAULT_UC);
        this->Unref();
    }

    Query::freeRequest(request);
}

void node_db::Query::freeRequest(execute_request_t* request, bool freeAll) {
    if (request->rows != NULL) {
        for (std::vector<row_t*>::iterator iterator = request->rows->begin(), end = request->rows->end(); iterator != end; ++iterator) {
            row_t* row = *iterator;
            for (uint16_t i = 0; i < request->columnCount; i++) {
                if (row->columns[i] != NULL) {
                    delete row->columns[i];
                }
            }
            delete [] row->columnLengths;
            delete [] row->columns;
            delete row;
        }

        delete request->rows;
    }

    if (freeAll) {
        if (request->result != NULL) {
            delete request->result;
        }

        delete request;
    }
}

v8::Handle<v8::Value> node_db::Query::set(const v8::Arguments& args) {
    if (args.Length() == 0) {
        return v8::Handle<v8::Value>();
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
        if (args[2]->IsFunction()) {
            ARG_CHECK_FUNCTION(2, callback);
            if (args[1]->IsArray()) {
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
            queryIndex = 0;
            valuesIndex = 1;
            optionsIndex = 2;
        }
    } else if (args.Length() == 2) {
        ARG_CHECK_STRING(0, query);
        queryIndex = 0;
        if (args[1]->IsFunction()) {
            ARG_CHECK_FUNCTION(1, callback);
            callbackIndex = 1;
        } else if (args[1]->IsArray()) {
            ARG_CHECK_ARRAY(1, values);
            valuesIndex = 1;
        } else {
            ARG_CHECK_OBJECT(1, options);
            optionsIndex = 1;
        }
    } else if (args.Length() == 1) {
        if (args[0]->IsString()) {
            ARG_CHECK_STRING(0, query);
            queryIndex = 0;
        } else if (args[0]->IsFunction()) {
            ARG_CHECK_FUNCTION(0, callback);
            callbackIndex = 0;
        } else if (args[0]->IsArray()) {
            ARG_CHECK_ARRAY(0, values);
            valuesIndex = 0;
        } else {
            ARG_CHECK_OBJECT(0, options);
            optionsIndex = 0;
        }
    }

    if (queryIndex >= 0) {
        v8::String::Utf8Value initialSql(args[queryIndex]->ToString());
        this->sql.str("");
        this->sql.clear();
        this->sql << *initialSql;
    }

    if (optionsIndex >= 0) {
        v8::Local<v8::Object> options = args[optionsIndex]->ToObject();

        ARG_CHECK_OBJECT_ATTR_OPTIONAL_BOOL(options, async);
        ARG_CHECK_OBJECT_ATTR_OPTIONAL_BOOL(options, cast);
        ARG_CHECK_OBJECT_ATTR_OPTIONAL_BOOL(options, bufferText);
        ARG_CHECK_OBJECT_ATTR_OPTIONAL_FUNCTION(options, start);
        ARG_CHECK_OBJECT_ATTR_OPTIONAL_FUNCTION(options, finish);

        if (options->Has(async_key)) {
            this->async = options->Get(async_key)->IsTrue();
        }

        if (options->Has(cast_key)) {
            this->cast = options->Get(cast_key)->IsTrue();
        }

        if (options->Has(bufferText_key)) {
            this->bufferText = options->Get(bufferText_key)->IsTrue();
        }

        if (options->Has(start_key)) {
            if (this->cbStart != NULL) {
                node::cb_destroy(this->cbStart);
            }
            this->cbStart = node::cb_persist(options->Get(start_key));
        }

        if (options->Has(finish_key)) {
            if (this->cbFinish != NULL) {
                node::cb_destroy(this->cbFinish);
            }
            this->cbFinish = node::cb_persist(options->Get(finish_key));
        }
    }

    if (valuesIndex >= 0) {
        v8::Local<v8::Array> values = v8::Array::Cast(*args[valuesIndex]);
        this->values = v8::Persistent<v8::Array>::New(values);
    }

    if (callbackIndex >= 0) {
        this->cbSuccess = node::cb_persist(args[callbackIndex]);
    }

    return v8::Handle<v8::Value>();
}

v8::Local<v8::Object> node_db::Query::row(node_db::Result* result, row_t* currentRow) const {
    v8::Local<v8::Object> row = v8::Object::New();

    for (uint16_t j = 0, limitj = result->columnCount(); j < limitj; j++) {
        node_db::Result::Column* currentColumn = result->column(j);
        v8::Local<v8::Value> value;

        if (currentRow->columns[j] != NULL) {
            const char* currentValue = currentRow->columns[j];
            uint64_t currentLength = currentRow->columnLengths[j];
            if (this->cast) {
                node_db::Result::Column::type_t columnType = currentColumn->getType();
                switch (columnType) {
                    case node_db::Result::Column::BOOL:
                        value = v8::Local<v8::Value>::New(currentValue == NULL || currentLength == 0 || currentValue[0] != '0' ? v8::True() : v8::False());
                        break;
                    case node_db::Result::Column::INT:
                        value = v8::String::New(currentValue, currentLength)->ToInteger();
                        break;
                    case node_db::Result::Column::NUMBER:
                        value = v8::Number::New(::atof(currentValue));
                        break;
                    case node_db::Result::Column::TIME:
                        {
                            int hour, min, sec;
                            sscanf(currentValue, "%d:%d:%d", &hour, &min, &sec);
                            value = v8::Date::New(static_cast<uint64_t>((hour*60*60 + min*60 + sec) * 1000));
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

                            value = v8::Date::New(static_cast<uint64_t>((mktime(&timeinfo) + Query::gmtDelta) * 1000));
                        } catch(const node_db::Exception&) {
                            value = v8::String::New(currentValue, currentLength);
                        }
                        break;
                    case node_db::Result::Column::SET:
                        {
                            v8::Local<v8::Array> values = v8::Array::New();
                            std::istringstream stream(currentValue);
                            std::string item;
                            uint64_t index = 0;
                            while (std::getline(stream, item, ',')) {
                                if (!item.empty()) {
                                    values->Set(v8::Integer::New(index++), v8::String::New(item.c_str()));
                                }
                            }
                            value = values;
                        }
                        break;
                    case node_db::Result::Column::TEXT:
                        if (this->bufferText || currentColumn->isBinary()) {
                            value = v8::Local<v8::Value>::New(node::Buffer::New(v8::String::New(currentValue, currentLength)));
                        } else {
                            value = v8::String::New(currentValue, currentLength);
                        }
                        break;
                    default:
                        value = v8::String::New(currentValue, currentLength);
                        break;
                }
            } else {
                value = v8::String::New(currentValue, currentLength);
            }
        } else {
            value = v8::Local<v8::Value>::New(v8::Null());
        }
        row->Set(v8::String::New(currentColumn->getName().c_str()), value);
    }

    return row;
}

std::string node_db::Query::parseQuery(const std::string& query, v8::Array* values) const throw(node_db::Exception&) {
    std::string parsed(query);
    std::vector<std::string::size_type> positions;
    char quote = 0;
    bool escaped = false;
    uint32_t delta = 0;

    for (std::string::size_type i = 0, limiti = query.length(); i < limiti; i++) {
        char currentChar = query[i];
        if (escaped) {
            if (currentChar == '?') {
                parsed.replace(i - 1 - delta, 1, "");
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

    uint32_t valuesLength = values != NULL ? values->Length() : 0;
    if (positions.size() != valuesLength) {
        throw node_db::Exception("Wrong number of values to escape");
    }

    uint32_t index = 0;
    delta = 0;
    for (std::vector<std::string::size_type>::iterator iterator = positions.begin(), end = positions.end(); iterator != end; ++iterator, index++) {
        std::string value = this->value(values->Get(index));
        parsed.replace(*iterator + delta, 1, value);
        delta += (value.length() - 1);
    }

    return parsed;
}

std::string node_db::Query::value(v8::Local<v8::Value> value, bool inArray, bool escape) const throw(node_db::Exception&) {
    std::ostringstream currentStream;

    if (value->IsArray()) {
        v8::Local<v8::Array> array = v8::Array::Cast(*value);
        if (!inArray) {
            currentStream << "(";
        }
        for (uint32_t i = 0, limiti = array->Length(); i < limiti; i++) {
            v8::Local<v8::Value> child = array->Get(i);
            if (child->IsArray() && i > 0) {
                currentStream << "),(";
            } else if (i > 0) {
                currentStream << ",";
            }

            currentStream << this->value(child, true, escape);
        }
        if (!inArray) {
            currentStream << ")";
        }
    } else if (value->IsDate()) {
        currentStream << this->connection->quoteString << this->fromDate(v8::Date::Cast(*value)->NumberValue()) << this->connection->quoteString;
    } else if (value->IsBoolean()) {
        currentStream << (value->IsTrue() ? "1" : "0");
    } else if (value->IsNumber()) {
        currentStream << value->ToNumber()->Value();
    } else if (value->IsString()) {
        v8::String::Utf8Value currentString(value->ToString());
        std::string string = *currentString;
        if (escape) {
            currentStream << this->connection->quoteString << this->connection->escape(string) << this->connection->quoteString;
        } else {
            currentStream << string;
        }
    }

    return currentStream.str();
}

std::string node_db::Query::fromDate(const uint64_t timeStamp) const throw(node_db::Exception&) {
    char* buffer = new char[20];
    if (buffer == NULL) {
        throw node_db::Exception("Can\'t create buffer to write parsed date");
    }


    struct tm timeinfo;
    time_t rawtime = timeStamp / 1000;
    if (!localtime_r(&rawtime, &timeinfo)) {
        throw node_db::Exception("Can't get local time");
    }

    strftime(buffer, 20, "%Y-%m-%d %H:%M:%S", &timeinfo);

    std::string date(buffer);
    delete [] buffer;

    return date;
}
