// Copyright 2011 Mariano Iglesias <mgiglesias@gmail.com>
#ifndef NODE_DEFS_H_
#define NODE_DEFS_H_

#include <napi.h>
#include <uv.h>
#include <assert.h>

template <typename T> T Global(T ref) {
  ref.SuppressDestruct();
  return ref;
}

#define NODE_CONSTANT(constant) Napi::Number::New(env, constant)
#define NODE_PERSISTENT_SYMBOL(s) Global(Persistent(Symbol::New(env, s)))

#define NODE_ADD_PROTOTYPE_METHOD(templ, name, callback)                  \
do {                                                                      \
  Napi::Env env = templ.Env();						\
  Napi::FunctionReference __callback##_TEM =				\
    Persistent(Napi::Function::New(env, callback));			\
    napi_value cproto;							\
    napi_get_prototype(env, templ.Value(), &cproto);			\
    Object proto(env, cproto);					  \
    proto.Set(name, __callback##_TEM);				  \
 } while (0)

#define NODE_ADD_CONSTANT(target, name, constant)                         \
  (target).Value().Set(#name, (double)constant)

#ifdef NAPI_CPP_EXCEPTIONS
# define THROW_EXCEPTION(message, ...)				\
    Error::New(env, message).ThrowAsJavaScriptException();
#else
# define THROW_EXCEPTION(message, ...)				\
  do { \
  Error::New(env, message).ThrowAsJavaScriptException(); \
  return __VA_ARGS__; \
  } while (0);
#endif

#define ARG_CHECK_OPTIONAL_STRING(I, VAR) \
    if (args.Length() > I && !args[I].IsString()) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" must be a valid string") \
    }

#define ARG_CHECK_STRING(I, VAR) \
    if (args.Length() <= I) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" is mandatory") \
    } else if (!args[I].IsString()) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" must be a valid string") \
    }

#define ARG_CHECK_OPTIONAL_BOOL(I, VAR) \
    if (args.Length() > I && !args[I].IsBoolean()) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" must be a valid boolean") \
    }

#define ARG_CHECK_BOOL(I, VAR) \
    if (args.Length() <= I) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" is mandatory") \
    } else if (!args[I].IsBoolean()) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" must be a valid boolean") \
    }

#define ARG_CHECK_OPTIONAL_UINT32(I, VAR) \
    if (args.Length() > I && !args[I].IsNumber()) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" must be a valid UINT32") \
    }

#define ARG_CHECK_UINT32(I, VAR) \
    if (args.Length() <= I) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" is mandatory") \
    } else if (!args[I].IsNumber()) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" must be a valid UINT32") \
    }

#define ARG_CHECK_OPTIONAL_OBJECT(I, VAR) \
    if (args.Length() > I && (!args[I].IsObject() || args[I].IsFunction() || args[I].IsUndefined())) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" must be a valid object") \
    }

#define ARG_CHECK_OBJECT(I, VAR) \
    if (args.Length() <= I) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" is mandatory") \
    } else if (!args[I].IsObject() || args[I].IsFunction() || args[I].IsArray() || args[I].IsUndefined()) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" must be a valid object") \
    }

#define ARG_CHECK_OPTIONAL_FUNCTION(I, VAR) \
    if (args.Length() > I && !args[I].IsFunction()) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" must be a valid function") \
    }

#define ARG_CHECK_FUNCTION(I, VAR) \
    if (args.Length() <= I) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" is mandatory") \
    } else if (!args[I].IsFunction()) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" must be a valid function") \
    }

#define ARG_CHECK_OPTIONAL_ARRAY(I, VAR) \
    if (args.Length() > I && !args[I].IsArray()) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" must be a valid array") \
    }

#define ARG_CHECK_ARRAY(I, VAR) \
    if (args.Length() <= I) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" is mandatory") \
    } else if (!args[I].IsArray()) { \
        THROW_EXCEPTION("Argument \"" #VAR "\" must be a valid array") \
    }

#define ARG_CHECK_OBJECT_ATTR_STRING(VAR, KEY) \
  Napi::String KEY##_##key = String::New(env, "" #KEY "");	\
    if (!VAR.Has(KEY##_##key)) { \
        THROW_EXCEPTION("Option \"" #KEY "\" is mandatory") \
    } else if (!VAR.Get(KEY##_##key).IsString()) { \
        THROW_EXCEPTION("Option \"" #KEY "\" must be a valid string") \
    }

#define ARG_CHECK_OBJECT_ATTR_OPTIONAL_STRING(VAR, KEY) \
  Napi::String KEY##_##key = String::New(env, "" #KEY "");	      \
    if (VAR.Has(KEY##_##key) && !VAR.Get(KEY##_##key).IsString()) { \
        THROW_EXCEPTION("Option \"" #KEY "\" must be a valid string") \
    }

#define ARG_CHECK_OBJECT_ATTR_UINT32(VAR, KEY) \
  Napi::String KEY##_##key = String::New(env, "" #KEY "");	\
    if (!VAR.Has(KEY##_##key)) { \
        THROW_EXCEPTION("Option \"" #KEY "\" is mandatory") \
    } else if (!VAR.Get(KEY##_##key).IsNumber()) { \
        THROW_EXCEPTION("Option \"" #KEY "\" must be a valid UINT32") \
    }

#define ARG_CHECK_OBJECT_ATTR_OPTIONAL_UINT32(VAR, KEY) \
  Napi::String KEY##_##key = String::New(env, "" #KEY "");	       \
    if (VAR.Has(KEY##_##key) && !VAR.Get(KEY##_##key).IsNumber()) { \
        THROW_EXCEPTION("Option \"" #KEY "\" must be a valid UINT32") \
    }

#define ARG_CHECK_OBJECT_ATTR_BOOL(VAR, KEY) \
  Napi::String KEY##_##key = String::New(env, "" #KEY "");	\
    if (!VAR.Has(KEY##_##key)) { \
        THROW_EXCEPTION("Option \"" #KEY "\" is mandatory") \
    } else if (!VAR.Get(KEY##_##key).IsBoolean()) { \
        THROW_EXCEPTION("Option \"" #KEY "\" must be a valid boolean") \
    }

#define ARG_CHECK_OBJECT_ATTR_OPTIONAL_BOOL(VAR, KEY) \
  Napi::String KEY##_##key = String::New(env, "" #KEY "");		\
    if (VAR.Has(KEY##_##key) && !VAR.Get(KEY##_##key).IsBoolean()) { \
        THROW_EXCEPTION("Option \"" #KEY "\" must be a valid boolean") \
    }

#define ARG_CHECK_OBJECT_ATTR_FUNCTION(VAR, KEY) \
  Napi::String KEY##_##key = String::New(env, "" #KEY "");	\
    if (!VAR.Has(KEY##_##key)) { \
        THROW_EXCEPTION("Option \"" #KEY "\" is mandatory") \
    } else if (!VAR.Get(KEY##_##key).IsFunction()) { \
        THROW_EXCEPTION("Option \"" #KEY "\" must be a valid function") \
    }

#define ARG_CHECK_OBJECT_ATTR_OPTIONAL_FUNCTION(VAR, KEY) \
  Napi::String KEY##_##key = String::New(env, "" #KEY "");		\
    if (VAR.Has(KEY##_##key) && !VAR.Get(KEY##_##key).IsFunction()) { \
        THROW_EXCEPTION("Option \"" #KEY "\" must be a valid function") \
    }

#endif  // NODE_DEFS_H_
