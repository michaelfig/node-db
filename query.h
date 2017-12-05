// Copyright 2011 Mariano Iglesias <mgiglesias@gmail.com>
#ifndef QUERY_H_
#define QUERY_H_

#include <stdlib.h>
#include <napi.h>
#include <uv.h>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <string>
#include <sstream>
#include <vector>
#include "./node_defs.h"
#include "./connection.h"
#include "./events.h"
#include "./exception.h"
#include "./result.h"

namespace node_db {
using namespace Napi;
class Query : public EventEmitter {
    public:
        static void Init(Object target, Napi::FunctionReference constructorTemplate);
        void setConnection(Connection* connection);
	Napi::Value set(const CallbackInfo& args);

    protected:
        struct row_t {
            char** columns;
            unsigned long* columnLengths;
        };
        struct execute_request_t {
            ObjectReference context;
            Query* query;
            Result* result;
            std::string* error;
            uint16_t columnCount;
            bool buffered;
            std::vector<row_t*>* rows;
        };
        Connection* connection;
        std::ostringstream sql;
        std::vector< Reference<Napi::Value> > values;
        bool async;
        bool cast;
        bool bufferText;
        FunctionReference* cbStart;
        FunctionReference* cbExecute;
        FunctionReference* cbFinish;

        Query();
        ~Query();
        static Napi::Value Select(const CallbackInfo& args);
        static Napi::Value From(const CallbackInfo& args);
        static Napi::Value Join(const CallbackInfo& args);
        static Napi::Value Where(const CallbackInfo& args);
        static Napi::Value And(const CallbackInfo& args);
        static Napi::Value Or(const CallbackInfo& args);
        static Napi::Value Order(const CallbackInfo& args);
        static Napi::Value Limit(const CallbackInfo& args);
        static Napi::Value Add(const CallbackInfo& args);
        static Napi::Value Insert(const CallbackInfo& args);
        static Napi::Value Update(const CallbackInfo& args);
        static Napi::Value Set(const CallbackInfo& args);
        static Napi::Value Delete(const CallbackInfo& args);
        static Napi::Value Sql(const CallbackInfo& args);
        static Napi::Value Execute(const CallbackInfo& args);
        static uv_async_t g_async;
        static void uvExecute(uv_work_t* uvRequest);
        static void uvExecuteFinished(uv_work_t* uvRequest, int status);
        void executeAsync(execute_request_t* request);
        static void freeRequest(execute_request_t* request, bool freeAll = true);
        std::string fieldName(Napi::Value value) const throw(Exception&);
        std::string tableName(Napi::Value value, bool escape = true) const throw(Exception&);
	Napi::Value addCondition(const CallbackInfo& args, const char* separator);
        Napi::Object row(Result* result, row_t* currentRow) const;
        virtual std::string parseQuery() const throw(Exception&);
        virtual std::vector<std::string::size_type> placeholders(std::string* parsed) const throw(Exception&);
        virtual Result* execute() const throw(Exception&);
        std::string value(Napi::Value value, bool inArray = false, bool escape = true, int precision = -1) const throw(Exception&);


    private:
        static bool gmtDeltaLoaded;
        static int gmtDelta;

        std::string fromDate(const double timeStamp) const throw(Exception&);
};
}

#endif  // QUERY_H_
