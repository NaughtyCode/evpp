#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "runtime/core/engine_api.h"

namespace engine {
namespace mongo {

// Wraps mongoc-oidc-callback.h — OpenID Connect (OIDC) authentication callback.
// The callback returns a MongoOidcCredential* (ownership transferred to the driver).

class MongoOidcCallbackParams;
class MongoOidcCredential;
class MongoOidcCallback;

using MongoOidcCallbackFn = std::function<MongoOidcCredential*(const MongoOidcCallbackParams& params)>;

// Read-only wrapper around mongoc_oidc_callback_params_t.
class ENGINE_API MongoOidcCallbackParams {
public:
    explicit MongoOidcCallbackParams(void* raw_params); // takes mongoc_oidc_callback_params_t*
    ~MongoOidcCallbackParams() = default;

    int32_t GetVersion() const;
    void* GetUserData() const;
    const int64_t* GetTimeout() const;
    const char* GetUsername() const;
    MongoOidcCredential* CancelWithTimeout();

private:
    void* params_; // mongoc_oidc_callback_params_t*
};

// Wraps mongoc_oidc_credential_t — OIDC access token credential.
class ENGINE_API MongoOidcCredential {
public:
    static MongoOidcCredential* New(const char* access_token);
    static MongoOidcCredential* NewWithExpiresIn(const char* access_token, int64_t expires_in);

    void Destroy();

    MongoOidcCredential(const MongoOidcCredential&) = delete;
    MongoOidcCredential& operator=(const MongoOidcCredential&) = delete;

    const char* GetAccessToken() const;
    const int64_t* GetExpiresIn() const;

    void* Raw(); // returns mongoc_oidc_credential_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MongoOidcCredential();
    ~MongoOidcCredential();
};

// Wraps mongoc_oidc_callback_t — bundles the callback function + user data.
class ENGINE_API MongoOidcCallback {
public:
    static MongoOidcCallback* New(MongoOidcCallbackFn fn);
    static MongoOidcCallback* NewWithUserData(MongoOidcCallbackFn fn, void* user_data);

    void Destroy();

    MongoOidcCallback(const MongoOidcCallback&) = delete;
    MongoOidcCallback& operator=(const MongoOidcCallback&) = delete;

    void* GetUserData() const;
    void SetUserData(void* user_data);

    const void* GetFn() const; // returns the C callback function pointer

    void* Raw(); // returns mongoc_oidc_callback_t*

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MongoOidcCallback();
    ~MongoOidcCallback();
};

} // namespace mongo
} // namespace engine
