#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_client_pool.h"
#include "runtime/core/mem/mem.h"

#include <cstdint>

#include "runtime/database/mongo/mongo_client.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/database/mongo/mongo_server_api.h"
#include "runtime/database/mongo/mongo_uri.h"

#include <mongoc/mongoc.h>

namespace engine {
namespace mongo {

struct MongoClientPool::Impl {
	mongoc_client_pool_t* pool = nullptr;
};

MongoClientPool* MongoClientPool::New(const MongoUri& uri) {
	if (!uri.RawUri()) return nullptr;
	auto* pool = CLOUDENGINE_MEM_NEW(MongoClientPool);
	pool->impl_->pool = mongoc_client_pool_new(static_cast<const mongoc_uri_t*>(uri.RawUri()));
	if (!pool->impl_->pool) {
		CLOUDENGINE_MEM_DELETE(pool);
		return nullptr;
	}
	return pool;
}

MongoClientPool* MongoClientPool::New(const MongoUri& uri, MongoError* error) {
	if (!uri.RawUri()) return nullptr;
	auto* pool = CLOUDENGINE_MEM_NEW(MongoClientPool);
	pool->impl_->pool = mongoc_client_pool_new_with_error(
		static_cast<const mongoc_uri_t*>(uri.RawUri()),
		error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
	if (!pool->impl_->pool) {
		CLOUDENGINE_MEM_DELETE(pool);
		return nullptr;
	}
	return pool;
}

MongoClientPool::MongoClientPool() : impl_(std::make_unique<Impl>()) {
}

MongoClientPool::~MongoClientPool() {
	if (impl_ && impl_->pool) mongoc_client_pool_destroy(impl_->pool);
}

void MongoClientPool::Destroy() {
	if (impl_ && impl_->pool) {
		mongoc_client_pool_destroy(impl_->pool);
		impl_->pool = nullptr;
	}
}

MongoClient* MongoClientPool::Pop() {
	if (!impl_ || !impl_->pool) return nullptr;
	mongoc_client_t* c = mongoc_client_pool_pop(impl_->pool);
	if (!c) return nullptr;
	// The pooled client must be returned via MongoClientPool::Push() before destruction.
	return MongoClient::FromPooled(c);
}

void MongoClientPool::Push(MongoClient* client) {
	if (!client) return;

	if (impl_ && impl_->pool && client->RawClient()) {
		mongoc_client_pool_push(impl_->pool,
								static_cast<mongoc_client_t*>(client->RawClient()));
		client->ReleaseFromPool();
	} else {
		client->Destroy();
	}
	CLOUDENGINE_MEM_DELETE(client);
}

MongoClient* MongoClientPool::TryPop() {
	if (!impl_ || !impl_->pool) return nullptr;
	mongoc_client_t* c = mongoc_client_pool_try_pop(impl_->pool);
	if (!c) return nullptr;
	return MongoClient::FromPooled(c);
}

void MongoClientPool::SetMaxSize(uint32_t max_pool_size) {
	if (impl_ && impl_->pool) mongoc_client_pool_max_size(impl_->pool, max_pool_size);
}

void MongoClientPool::SetSslOpts(const void* ssl_opts) {
#ifdef MONGOC_ENABLE_SSL
	if (impl_ && impl_->pool)
		mongoc_client_pool_set_ssl_opts(impl_->pool,
										static_cast<const mongoc_ssl_opt_t*>(ssl_opts));
#endif
}

bool MongoClientPool::SetApmCallbacks(void* callbacks, void* context) {
	return impl_ && impl_->pool &&
		   mongoc_client_pool_set_apm_callbacks(
			   impl_->pool, static_cast<mongoc_apm_callbacks_t*>(callbacks), context);
}

bool MongoClientPool::SetErrorApi(uint32_t version) {
	if (version > static_cast<uint32_t>(INT32_MAX)) return false;
	return impl_ && impl_->pool &&
		   mongoc_client_pool_set_error_api(impl_->pool, static_cast<int32_t>(version));
}

bool MongoClientPool::SetAppname(const char* appname) {
	return impl_ && impl_->pool && mongoc_client_pool_set_appname(impl_->pool, appname);
}

bool MongoClientPool::SetServerApi(const MongoServerApi& api, MongoError* error) {
	return impl_ && impl_->pool &&
		   mongoc_client_pool_set_server_api(
			   impl_->pool,
			   static_cast<const mongoc_server_api_t*>(api.RawServerApi()),
			   error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoClientPool::AppendMetadata(const char* name, const char* version, const char* platform) {
	return impl_ && impl_->pool &&
		   mongoc_client_pool_append_metadata(impl_->pool, name, version, platform);
}

bool MongoClientPool::EnableAutoEncryption(void* opts, MongoError* error) {
	return impl_ && impl_->pool &&
		   mongoc_client_pool_enable_auto_encryption(
			   impl_->pool,
			   static_cast<mongoc_auto_encryption_opts_t*>(opts),
			   error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoClientPool::SetStructuredLogOpts(const void* opts) {
	return impl_ && impl_->pool &&
		   mongoc_client_pool_set_structured_log_opts(
			   impl_->pool, static_cast<const mongoc_structured_log_opts_t*>(opts));
}

bool MongoClientPool::SetOidcCallback(const void* callback) {
	return impl_ && impl_->pool &&
		   mongoc_client_pool_set_oidc_callback(
			   impl_->pool, static_cast<const mongoc_oidc_callback_t*>(callback));
}

void* MongoClientPool::RawPool() {
	return impl_ ? impl_->pool : nullptr;
}

}  // namespace mongo
}  // namespace engine

#endif
