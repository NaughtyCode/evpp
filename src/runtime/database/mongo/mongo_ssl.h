#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include <memory>
#include <string>

#include "runtime/core/engine_api.h"

namespace engine {
namespace mongo {

// Wraps mongoc-ssl.h — SSL/TLS configuration for client connections.
class CLOUD_ENGINE_API MongoSslOpts {
	public:
	MongoSslOpts();
	~MongoSslOpts();

	MongoSslOpts(const MongoSslOpts& other);
	MongoSslOpts& operator=(const MongoSslOpts& other);
	MongoSslOpts(MongoSslOpts&&) noexcept;
	MongoSslOpts& operator=(MongoSslOpts&&) noexcept;

	void SetPemFile(const char* pem_file);
	void SetPemPwd(const char* pem_pwd);
	void SetCaFile(const char* ca_file);
	void SetCaDir(const char* ca_dir);
	void SetCrlFile(const char* crl_file);
	void SetWeakCertValidation(bool weak);
	void SetAllowInvalidHostname(bool allow);

	const char* GetPemFile() const;
	const char* GetPemPwd() const;
	const char* GetCaFile() const;
	const char* GetCaDir() const;
	const char* GetCrlFile() const;
	bool GetWeakCertValidation() const;
	bool GetAllowInvalidHostname() const;

	static const void* GetDefault();  // returns const mongoc_ssl_opt_t*

	void* Raw();  // returns mongoc_ssl_opt_t*

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

}  // namespace mongo
}  // namespace engine

#endif
