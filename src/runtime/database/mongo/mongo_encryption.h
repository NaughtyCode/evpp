#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include <cstdint>
#include <functional>
#include <memory>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_forward.h"

namespace engine {
namespace mongo {

// KMS credentials provider callback type

using MongoKmsCredentialsProviderCb = std::function<bool(
	void* userdata, const BsonDocument& params, BsonDocument* out, MongoError* error)>;

// Auto-Encryption Options

class ENGINE_API MongoAutoEncryptionOpts {
	public:
	MongoAutoEncryptionOpts();
	~MongoAutoEncryptionOpts();

	MongoAutoEncryptionOpts(const MongoAutoEncryptionOpts&) = delete;
	MongoAutoEncryptionOpts& operator=(const MongoAutoEncryptionOpts&) = delete;
	MongoAutoEncryptionOpts(MongoAutoEncryptionOpts&&) noexcept;
	MongoAutoEncryptionOpts& operator=(MongoAutoEncryptionOpts&&) noexcept;

	void SetKeyvaultClient(void* client);  // mongoc_client_t*
	void SetKeyvaultClientPool(void* pool);	 // mongoc_client_pool_t*
	void SetKeyvaultNamespace(const char* db, const char* coll);
	void SetKmsProviders(const BsonDocument& kms_providers);
	void SetKeyExpiration(uint64_t expiration_ms);
	void SetTlsOpts(const BsonDocument& tls_opts);
	void SetSchemaMap(const BsonDocument& schema_map);
	void SetEncryptedFieldsMap(const BsonDocument& encrypted_fields_map);
	void SetBypassAutoEncryption(bool bypass);
	void SetBypassQueryAnalysis(bool bypass);
	void SetExtra(const BsonDocument& extra);
	void SetKmsCredentialProviderCallback(MongoKmsCredentialsProviderCb cb, void* userdata);

	void* Raw();

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// Client Encryption Options (for explicit encryption)

class ENGINE_API MongoClientEncryptionOpts {
	public:
	MongoClientEncryptionOpts();
	~MongoClientEncryptionOpts();

	MongoClientEncryptionOpts(const MongoClientEncryptionOpts&) = delete;
	MongoClientEncryptionOpts& operator=(const MongoClientEncryptionOpts&) = delete;
	MongoClientEncryptionOpts(MongoClientEncryptionOpts&&) noexcept;
	MongoClientEncryptionOpts& operator=(MongoClientEncryptionOpts&&) noexcept;

	void SetKeyvaultClient(void* keyvault_client);
	void SetKeyvaultNamespace(const char* db, const char* coll);
	void SetKmsProviders(const BsonDocument& kms_providers);
	void SetTlsOpts(const BsonDocument& tls_opts);
	void SetKmsCredentialProviderCallback(MongoKmsCredentialsProviderCb cb, void* userdata);
	void SetKeyExpiration(uint64_t cache_expiration_ms);

	void* Raw();

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// Encrypt Options

class ENGINE_API MongoClientEncryptionEncryptOpts {
	public:
	MongoClientEncryptionEncryptOpts();
	~MongoClientEncryptionEncryptOpts();

	MongoClientEncryptionEncryptOpts(const MongoClientEncryptionEncryptOpts&) = delete;
	MongoClientEncryptionEncryptOpts& operator=(const MongoClientEncryptionEncryptOpts&) = delete;
	MongoClientEncryptionEncryptOpts(MongoClientEncryptionEncryptOpts&&) noexcept;
	MongoClientEncryptionEncryptOpts& operator=(MongoClientEncryptionEncryptOpts&&) noexcept;

	void SetKeyId(const void* keyid);  // bson_value_t*
	void SetKeyAltName(const char* keyaltname);
	void SetAlgorithm(const char* algorithm);
	void SetContentionFactor(int64_t factor);
	void SetQueryType(const char* query_type);

	// FLE2 — Range and Text encryption
	void SetRangeOpts(const void* range_opts);	// MongoClientEncryptionEncryptRangeOpts*
	void SetTextOpts(const void* text_opts);  // MongoClientEncryptionEncryptTextOpts*

	void* Raw();

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// FLE2 Range Encryption Options

class ENGINE_API MongoClientEncryptionEncryptRangeOpts {
	public:
	MongoClientEncryptionEncryptRangeOpts();
	~MongoClientEncryptionEncryptRangeOpts();

	MongoClientEncryptionEncryptRangeOpts(const MongoClientEncryptionEncryptRangeOpts&) = delete;
	MongoClientEncryptionEncryptRangeOpts& operator=(const MongoClientEncryptionEncryptRangeOpts&) =
		delete;
	MongoClientEncryptionEncryptRangeOpts(MongoClientEncryptionEncryptRangeOpts&&) noexcept;
	MongoClientEncryptionEncryptRangeOpts& operator=(
		MongoClientEncryptionEncryptRangeOpts&&) noexcept;

	void SetTrimFactor(int32_t trim_factor);
	void SetSparsity(int64_t sparsity);
	void SetMin(const void* min);  // bson_value_t*
	void SetMax(const void* max);  // bson_value_t*
	void SetPrecision(int32_t precision);

	void* Raw();

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// FLE2 Text Encryption Options — Prefix

class ENGINE_API MongoClientEncryptionEncryptTextPrefixOpts {
	public:
	MongoClientEncryptionEncryptTextPrefixOpts();
	~MongoClientEncryptionEncryptTextPrefixOpts();

	MongoClientEncryptionEncryptTextPrefixOpts(const MongoClientEncryptionEncryptTextPrefixOpts&) =
		delete;
	MongoClientEncryptionEncryptTextPrefixOpts& operator=(
		const MongoClientEncryptionEncryptTextPrefixOpts&) = delete;
	MongoClientEncryptionEncryptTextPrefixOpts(
		MongoClientEncryptionEncryptTextPrefixOpts&&) noexcept;
	MongoClientEncryptionEncryptTextPrefixOpts& operator=(
		MongoClientEncryptionEncryptTextPrefixOpts&&) noexcept;

	void SetStrMaxQueryLength(int32_t len);
	void SetStrMinQueryLength(int32_t len);

	void* Raw();

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// FLE2 Text Encryption Options — Suffix

class ENGINE_API MongoClientEncryptionEncryptTextSuffixOpts {
	public:
	MongoClientEncryptionEncryptTextSuffixOpts();
	~MongoClientEncryptionEncryptTextSuffixOpts();

	MongoClientEncryptionEncryptTextSuffixOpts(const MongoClientEncryptionEncryptTextSuffixOpts&) =
		delete;
	MongoClientEncryptionEncryptTextSuffixOpts& operator=(
		const MongoClientEncryptionEncryptTextSuffixOpts&) = delete;
	MongoClientEncryptionEncryptTextSuffixOpts(
		MongoClientEncryptionEncryptTextSuffixOpts&&) noexcept;
	MongoClientEncryptionEncryptTextSuffixOpts& operator=(
		MongoClientEncryptionEncryptTextSuffixOpts&&) noexcept;

	void SetStrMaxQueryLength(int32_t len);
	void SetStrMinQueryLength(int32_t len);

	void* Raw();

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// FLE2 Text Encryption Options — Substring

class ENGINE_API MongoClientEncryptionEncryptTextSubstringOpts {
	public:
	MongoClientEncryptionEncryptTextSubstringOpts();
	~MongoClientEncryptionEncryptTextSubstringOpts();

	MongoClientEncryptionEncryptTextSubstringOpts(
		const MongoClientEncryptionEncryptTextSubstringOpts&) = delete;
	MongoClientEncryptionEncryptTextSubstringOpts& operator=(
		const MongoClientEncryptionEncryptTextSubstringOpts&) = delete;
	MongoClientEncryptionEncryptTextSubstringOpts(
		MongoClientEncryptionEncryptTextSubstringOpts&&) noexcept;
	MongoClientEncryptionEncryptTextSubstringOpts& operator=(
		MongoClientEncryptionEncryptTextSubstringOpts&&) noexcept;

	void SetStrMaxLength(int32_t len);
	void SetStrMaxQueryLength(int32_t len);
	void SetStrMinQueryLength(int32_t len);

	void* Raw();

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// FLE2 Text Encryption Options (aggregates prefix/suffix/substring)

class ENGINE_API MongoClientEncryptionEncryptTextOpts {
	public:
	MongoClientEncryptionEncryptTextOpts();
	~MongoClientEncryptionEncryptTextOpts();

	MongoClientEncryptionEncryptTextOpts(const MongoClientEncryptionEncryptTextOpts&) = delete;
	MongoClientEncryptionEncryptTextOpts& operator=(const MongoClientEncryptionEncryptTextOpts&) =
		delete;
	MongoClientEncryptionEncryptTextOpts(MongoClientEncryptionEncryptTextOpts&&) noexcept;
	MongoClientEncryptionEncryptTextOpts& operator=(
		MongoClientEncryptionEncryptTextOpts&&) noexcept;

	void SetPrefix(const void* popts);	// MongoClientEncryptionEncryptTextPrefixOpts*
	void SetSuffix(const void* sopts);	// MongoClientEncryptionEncryptTextSuffixOpts*
	void SetSubstring(const void* ssopts);	// MongoClientEncryptionEncryptTextSubstringOpts*
	void SetCaseSensitive(bool val);
	void SetDiacriticSensitive(bool val);

	void* Raw();

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// Data Key Options

class ENGINE_API MongoClientEncryptionDatakeyOpts {
	public:
	MongoClientEncryptionDatakeyOpts();
	~MongoClientEncryptionDatakeyOpts();

	MongoClientEncryptionDatakeyOpts(const MongoClientEncryptionDatakeyOpts&) = delete;
	MongoClientEncryptionDatakeyOpts& operator=(const MongoClientEncryptionDatakeyOpts&) = delete;
	MongoClientEncryptionDatakeyOpts(MongoClientEncryptionDatakeyOpts&&) noexcept;
	MongoClientEncryptionDatakeyOpts& operator=(MongoClientEncryptionDatakeyOpts&&) noexcept;

	void SetMasterkey(const BsonDocument& masterkey);
	void SetKeyAltNames(char** keyaltnames, uint32_t count);
	void SetKeyMaterial(const uint8_t* data, uint32_t len);

	void* Raw();
	const void* Raw() const;

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// Rewrap Many Data Key Result

class ENGINE_API MongoClientEncryptionRewrapManyDatakeyResult {
	public:
	MongoClientEncryptionRewrapManyDatakeyResult();
	~MongoClientEncryptionRewrapManyDatakeyResult();

	MongoClientEncryptionRewrapManyDatakeyResult(
		const MongoClientEncryptionRewrapManyDatakeyResult&) = delete;
	MongoClientEncryptionRewrapManyDatakeyResult& operator=(
		const MongoClientEncryptionRewrapManyDatakeyResult&) = delete;
	MongoClientEncryptionRewrapManyDatakeyResult(
		MongoClientEncryptionRewrapManyDatakeyResult&&) noexcept;
	MongoClientEncryptionRewrapManyDatakeyResult& operator=(
		MongoClientEncryptionRewrapManyDatakeyResult&&) noexcept;

	const void* GetBulkWriteResult() const;	 // returns const bson_t*
	void* Raw();

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// Client Encryption (explicit encryption)

class ENGINE_API MongoClientEncryption {
	public:
	static MongoClientEncryption* New(MongoClientEncryptionOpts* opts, MongoError* error);

	void Destroy();

	MongoClientEncryption(const MongoClientEncryption&) = delete;
	MongoClientEncryption& operator=(const MongoClientEncryption&) = delete;
	MongoClientEncryption(MongoClientEncryption&&) = delete;
	MongoClientEncryption& operator=(MongoClientEncryption&&) = delete;

	// ── Data key management ──────────────────────────────────────────
	bool CreateDatakey(const char* kms_provider,
					   const MongoClientEncryptionDatakeyOpts* opts,
					   void* keyid_out,	 // bson_value_t*
					   MongoError* error);
	bool RewrapManyDatakey(const BsonDocument& filter,
						   const char* provider,
						   const BsonDocument* master_key,
						   MongoClientEncryptionRewrapManyDatakeyResult* result,
						   MongoError* error);
	bool DeleteKey(const void* keyid, BsonDocument* reply, MongoError* error);
	bool GetKey(const void* keyid, BsonDocument* key_doc, MongoError* error);
	MongoCursor* GetKeys(MongoError* error);
	bool AddKeyAltName(const void* keyid,
					   const char* keyaltname,
					   BsonDocument* key_doc,
					   MongoError* error);
	bool RemoveKeyAltName(const void* keyid,
						  const char* keyaltname,
						  BsonDocument* key_doc,
						  MongoError* error);
	bool GetKeyByAltName(const char* keyaltname, BsonDocument* key_doc, MongoError* error);

	// ── Encrypt / Decrypt ────────────────────────────────────────────
	bool Encrypt(const void* value,	 // bson_value_t*
				 MongoClientEncryptionEncryptOpts* opts,
				 void* ciphertext_out,	// bson_value_t*
				 MongoError* error);
	bool EncryptExpression(const BsonDocument& expr,
						   MongoClientEncryptionEncryptOpts* opts,
						   BsonDocument* expr_out,
						   MongoError* error);
	bool Decrypt(const void* ciphertext,  // bson_value_t*
				 void* value_out,  // bson_value_t*
				 MongoError* error);

	// ── Encrypted collection ─────────────────────────────────────────
	void* CreateEncryptedCollection(void* database,
									const char* name,
									const BsonDocument* in_options,
									BsonDocument* out_options,
									const char* kms_provider,
									const BsonDocument* opt_masterkey,
									MongoError* error);

	// ── Version ─────────────────────────────────────────────────────
	const char* GetCryptSharedVersion() const;

	void* Raw();  // returns mongoc_client_encryption_t*

	private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
	MongoClientEncryption();
	~MongoClientEncryption();
};

}  // namespace mongo
}  // namespace engine

#endif
