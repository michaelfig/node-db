// Copyright 2011 Mariano Iglesias <mgiglesias@gmail.com>
#ifndef EVENTS_H_
#define EVENTS_H_

#include <napi.h>
#include "./node_defs.h"

namespace node_db {
  using namespace Napi;
  class EventEmitter : public ObjectWrap<EventEmitter> {
    public:
        static void Init();

    protected:
        static Reference<String> syEmit;

        EventEmitter();
        bool Emit(const char* event, int argc, Napi::Value argv[]);
};
}

#endif  // BINDING_H_
