#include "runtime/database/mongo/mongo_oidc.h"

#include <mongoc/mongoc.h>

#include <mutex>
#include <unordered_map>

namespace engine {
namespace mongo {

// ═══════════════════════════════════════════════════════════════════════
// MongoOidcCredential::Impl (needed by MongoOidcCallbackParams)
// ═══════════════════════════════════════════════════════════════════════

struct MongoOidcCredential::Impl {
    mongoc_oidc_credential_t* cred = nullptr;
    bool owned = true;
};

// ═══════════════════════════════════════════════════════════════════════
// MongoOidcCallbackParams
// ═══════════════════════════════════════════════════════════════════════

MongoOidcCallbackParams::MongoOidcCallbackParams(void* raw_params) : params_(raw_params) {}

int32_t MongoOidcCallbackParams::GetVersion() const {
    return mongoc_oidc_callback_params_get_version(
        static_cast<mongoc_oidc_callback_params_t*>(params_));
}

void* MongoOidcCallbackParams::GetUserData() const {
    return mongoc_oidc_callback_params_get_user_data(
        static_cast<mongoc_oidc_callback_params_t*>(params_));
}

const int64_t* MongoOidcCallbackParams::GetTimeout() const {
    return mongoc_oidc_callback_params_get_timeout(
        static_cast<mongoc_oidc_callback_params_t*>(params_));
}

const char* MongoOidcCallbackParams::GetUsername() const {
    return mongoc_oidc_callback_params_get_username(
        static_cast<mongoc_oidc_callback_params_t*>(params_));
}

MongoOidcCredential* MongoOidcCallbackParams::CancelWithTimeout() {
    auto* raw = mongoc_oidc_callback_params_cancel_with_timeout(
        static_cast<mongoc_oidc_callback_params_t*>(params_));
    if (!raw) return nullptr;
    auto* cred = new MongoOidcCredential();
    cred->impl_->owned = false;
    cred->impl_->cred = raw;
    return cred;
}

// ═══════════════════════════════════════════════════════════════════════
// MongoOidcCredential
// ═══════════════════════════════════════════════════════════════════════

MongoOidcCredential* MongoOidcCredential::New(const char* access_token) {
    auto* c = new MongoOidcCredential();
    c->impl_->cred = mongoc_oidc_credential_new(access_token);
    if (!c->impl_->cred) { delete c; return nullptr; }
    return c;
}

MongoOidcCredential* MongoOidcCredential::NewWithExpiresIn(const char* access_token, int64_t expires_in) {
    auto* c = new MongoOidcCredential();
    c->impl_->cred = mongoc_oidc_credential_new_with_expires_in(access_token, expires_in);
    if (!c->impl_->cred) { delete c; return nullptr; }
    return c;
}

MongoOidcCredential::MongoOidcCredential() : impl_(std::make_unique<Impl>()) {}
MongoOidcCredential::~MongoOidcCredential() { Destroy(); }

void MongoOidcCredential::Destroy() {
    if (impl_ && impl_->cred && impl_->owned) {
        mongoc_oidc_credential_destroy(impl_->cred);
        impl_->cred = nullptr;
    }
}

const char* MongoOidcCredential::GetAccessToken() const {
    return impl_ && impl_->cred ? mongoc_oidc_credential_get_access_token(impl_->cred) : nullptr;
}

const int64_t* MongoOidcCredential::GetExpiresIn() const {
    return impl_ && impl_->cred ? mongoc_oidc_credential_get_expires_in(impl_->cred) : nullptr;
}

void* MongoOidcCredential::Raw() { return impl_ ? impl_->cred : nullptr; }

void* MongoOidcCredential::ReleaseRaw() {
    if (impl_) {
        impl_->owned = false;
        return impl_->cred;
    }
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// MongoOidcCallback
// ═══════════════════════════════════════════════════════════════════════

namespace {

struct OidcCtx {
    MongoOidcCallbackFn fn;
    void* user_data = nullptr;
};

mongoc_oidc_credential_t* oidc_trampoline(mongoc_oidc_callback_params_t* params) {
    auto* ctx = static_cast<OidcCtx*>(mongoc_oidc_callback_params_get_user_data(params));
    if (!ctx || !ctx->fn) return nullptr;
    MongoOidcCallbackParams wrapper(params);
    MongoOidcCredential* cred = ctx->fn(wrapper);
    if (!cred) return nullptr;
    auto* raw = static_cast<mongoc_oidc_credential_t*>(cred->ReleaseRaw());
    delete cred;
    return raw;
}

} // namespace

struct MongoOidcCallback::Impl {
    mongoc_oidc_callback_t* cb = nullptr;
    std::shared_ptr<OidcCtx> ctx;
};

MongoOidcCallback* MongoOidcCallback::New(MongoOidcCallbackFn fn) {
    return NewWithUserData(std::move(fn), nullptr);
}

MongoOidcCallback* MongoOidcCallback::NewWithUserData(MongoOidcCallbackFn fn, void* user_data) {
    auto* c = new MongoOidcCallback();
    c->impl_->ctx = std::make_shared<OidcCtx>();
    c->impl_->ctx->fn = std::move(fn);
    c->impl_->ctx->user_data = user_data;
    c->impl_->cb = mongoc_oidc_callback_new_with_user_data(oidc_trampoline, c->impl_->ctx.get());
    if (!c->impl_->cb) { delete c; return nullptr; }
    return c;
}

MongoOidcCallback::MongoOidcCallback() : impl_(std::make_unique<Impl>()) {}
MongoOidcCallback::~MongoOidcCallback() { Destroy(); }

void MongoOidcCallback::Destroy() {
    if (impl_ && impl_->cb) {
        mongoc_oidc_callback_destroy(impl_->cb);
        impl_->cb = nullptr;
    }
}

void* MongoOidcCallback::GetUserData() const {
    return impl_->ctx ? impl_->ctx->user_data : nullptr;
}

void MongoOidcCallback::SetUserData(void* user_data) {
    if (impl_ && impl_->cb) {
        impl_->ctx->user_data = user_data;
        mongoc_oidc_callback_set_user_data(impl_->cb, impl_->ctx.get());
    }
}

const void* MongoOidcCallback::GetFn() const {
    return impl_ && impl_->cb ? mongoc_oidc_callback_get_fn(impl_->cb) : nullptr;
}

void* MongoOidcCallback::Raw() { return impl_ ? impl_->cb : nullptr; }

} // namespace mongo
} // namespace engine
