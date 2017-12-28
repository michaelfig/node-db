// Copyright 2011 Mariano Iglesias <mgiglesias@gmail.com>
#ifndef EVENTS_H_
#define EVENTS_H_

#include <napi.h>
#include "./node_defs.h"

namespace node_db {
  using namespace Napi;
  class EventEmitter {
    protected:
        bool Emit(const char* event, int argc, Napi::Value argv[]);

    public:
	virtual Napi::Object Value() = 0;
	virtual Napi::Env Env() = 0;
};
}

#endif  // BINDING_H_
