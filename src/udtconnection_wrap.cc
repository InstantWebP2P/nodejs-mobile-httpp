#include "udtconnection_wrap.h"

#include "env-inl.h"
#include "pipe_wrap.h"
#include "stream_base-inl.h"
#include "udtconnect_wrap.h"
#include "udt_wrap.h"
#include "util-inl.h"

namespace node {

using v8::Boolean;
using v8::Context;
using v8::HandleScope;
using v8::Integer;
using v8::Local;
using v8::Object;
using v8::Value;


template <typename WrapType, typename UVType>
UDTConnectionWrap<WrapType, UVType>::UDTConnectionWrap(Environment* env,
                                                       Local<Object> object,
                                                       ProviderType provider)
    : UDTStreamWrap(env,
                    object,
                    reinterpret_cast<uvudt_t*>(&handle_),
                    provider) {}


template <typename WrapType, typename UVType>
void UDTConnectionWrap<WrapType, UVType>::OnConnection(uvudt_t* handle,
                                                       int status) {
  WrapType* wrap_data = static_cast<WrapType*>(reinterpret_cast<uv_handle_t*>(handle)->data);
  CHECK_NOT_NULL(wrap_data);
  CHECK_EQ(&wrap_data->handle_, reinterpret_cast<UVType*>(handle));

  Environment* env = wrap_data->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  // We should not be getting this callback if someone has already called
  // uv_close() on the handle.
  CHECK_EQ(wrap_data->persistent().IsEmpty(), false);

  Local<Value> client_handle;

  if (status == 0) {
    // Instantiate the client javascript object and handle.
    Local<Object> client_obj;
    if (!WrapType::Instantiate(env, wrap_data, WrapType::SOCKET).ToLocal(&client_obj))
      return;

    // Unwrap the client javascript object.
    WrapType* wrap;
    ASSIGN_OR_RETURN_UNWRAP(&wrap, client_obj);
    uvudt_t* client = reinterpret_cast<uvudt_t*>(&wrap->handle_);
    // uvudt_accept can fail if the new connection has already been closed, in
    // which case an EAGAIN (resource temporarily unavailable) will be
    // returned.
    if (uvudt_accept(handle, client))
      return;

    // Successful accept. Call the onconnection callback in JavaScript land.
    client_handle = client_obj;
  } else {
    client_handle = Undefined(env->isolate());
  }

  Local<Value> argv[] = { Integer::New(env->isolate(), status), client_handle };
  wrap_data->MakeCallback(env->onconnection_string(), arraysize(argv), argv);
}


template <typename WrapType, typename UVType>
void UDTConnectionWrap<WrapType, UVType>::AfterConnect(uvudt_connect_t* req,
                                                       int status) {
  std::unique_ptr<UDTConnectWrap> req_wrap
    (static_cast<UDTConnectWrap*>(req->data));
  CHECK_NOT_NULL(req_wrap);
  WrapType* wrap = static_cast<WrapType*>(reinterpret_cast<uv_handle_t*>(req->handle)->data);
  CHECK_EQ(req_wrap->env(), wrap->env());
  Environment* env = wrap->env();

  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  // The wrap and request objects should still be there.
  CHECK_EQ(req_wrap->persistent().IsEmpty(), false);
  CHECK_EQ(wrap->persistent().IsEmpty(), false);

  bool readable, writable;

  if (status) {
    readable = writable = false;
  } else {
    readable = uvudt_is_readable(req->handle) != 0;
    writable = uvudt_is_writable(req->handle) != 0;
  }

  Local<Value> argv[5] = {
    Integer::New(env->isolate(), status),
    wrap->object(),
    req_wrap->object(),
    Boolean::New(env->isolate(), readable),
    Boolean::New(env->isolate(), writable)
  };

  req_wrap->MakeCallback(env->oncomplete_string(), arraysize(argv), argv);
}

template UDTConnectionWrap<UDTWrap, uvudt_t>::UDTConnectionWrap(
    Environment* env, 
    Local<Object> object, 
    ProviderType provider);

template void UDTConnectionWrap<UDTWrap, uvudt_t>::OnConnection(
    uvudt_t* handle,
    int status);

template void UDTConnectionWrap<UDTWrap, uvudt_t>::AfterConnect(
    uvudt_connect_t* handle, 
    int status);

}  // namespace node
