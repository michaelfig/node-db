// Copyright 2011 Mariano Iglesias <mgiglesias@gmail.com>
#include "./events.h"

bool node_db::EventEmitter::Emit(const char* event, int argc, Napi::Value argv[]) {
    Napi::Env env = Env();
    HandleScope scope(env);

    int nArgc = argc + 1;
    std::vector<napi_value> nArgv(nArgc);

    nArgv[0] = String::New(env, event);
    for (int i=0; i < argc; i++) {
        nArgv[i + 1] = argv[i];
    }

    Napi::Value emit_v = Value().Get("emit");
    if (!emit_v.IsFunction()) {
        return false;
    }
    Napi::Function emit = emit_v.As<Napi::Function>();

    emit.MakeCallback(Value(), nArgv);

    return true;
}
