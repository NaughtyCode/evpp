#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_bulk.h"
#include "runtime/core/mem/mem.h"

#include "runtime/database/mongo/mongo_bson.h"
#include "runtime/core/mem/mem.h"
#include "runtime/database/mongo/mongo_error.h"
#include "runtime/core/mem/mem.h"
#include "runtime/database/mongo/mongo_settings.h"
#include "runtime/core/mem/mem.h"

#include <mongoc/mongoc.h>

namespace engine {
namespace mongo {

struct MongoBulkOperation::Impl {
	mongoc_bulk_operation_t* bulk = nullptr;
};

MongoBulkOperation* MongoBulkOperation::New(bool ordered) {
	auto* op = MEM_NEW(MongoBulkOperation);
	op->impl_->bulk = mongoc_bulk_operation_new(ordered);
	if (!op->impl_->bulk) {
		MEM_DELETE(op);
		return nullptr;
	}
	return op;
}

MongoBulkOperation::MongoBulkOperation() : impl_(std::make_unique<Impl>()) {
}
MongoBulkOperation::~MongoBulkOperation() {
	Destroy();
}

void MongoBulkOperation::Destroy() {
	if (impl_ && impl_->bulk) {
		mongoc_bulk_operation_destroy(impl_->bulk);
		impl_->bulk = nullptr;
	}
	MEM_DELETE(this);
}

void MongoBulkOperation::Insert(const BsonDocument& document) {
	if (impl_ && impl_->bulk)
		mongoc_bulk_operation_insert(impl_->bulk, static_cast<const bson_t*>(document.RawBson()));
}

void MongoBulkOperation::Remove(const BsonDocument& selector) {
	if (impl_ && impl_->bulk)
		mongoc_bulk_operation_remove(impl_->bulk, static_cast<const bson_t*>(selector.RawBson()));
}

void MongoBulkOperation::RemoveOne(const BsonDocument& selector) {
	if (impl_ && impl_->bulk)
		mongoc_bulk_operation_remove_one(impl_->bulk,
										 static_cast<const bson_t*>(selector.RawBson()));
}

void MongoBulkOperation::ReplaceOne(const BsonDocument& selector,
									const BsonDocument& document,
									bool upsert) {
	if (impl_ && impl_->bulk)
		mongoc_bulk_operation_replace_one(impl_->bulk,
										  static_cast<const bson_t*>(selector.RawBson()),
										  static_cast<const bson_t*>(document.RawBson()),
										  upsert);
}

void MongoBulkOperation::Update(const BsonDocument& selector,
								const BsonDocument& document,
								bool upsert) {
	if (impl_ && impl_->bulk)
		mongoc_bulk_operation_update(impl_->bulk,
									 static_cast<const bson_t*>(selector.RawBson()),
									 static_cast<const bson_t*>(document.RawBson()),
									 upsert);
}

void MongoBulkOperation::UpdateOne(const BsonDocument& selector,
								   const BsonDocument& document,
								   bool upsert) {
	if (impl_ && impl_->bulk)
		mongoc_bulk_operation_update_one(impl_->bulk,
										 static_cast<const bson_t*>(selector.RawBson()),
										 static_cast<const bson_t*>(document.RawBson()),
										 upsert);
}

bool MongoBulkOperation::InsertWithOpts(const BsonDocument& document,
										const BsonDocument* opts,
										MongoError* error) {
	if (!impl_ || !impl_->bulk) return false;
	return mongoc_bulk_operation_insert_with_opts(
		impl_->bulk,
		static_cast<const bson_t*>(document.RawBson()),
		opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
		error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoBulkOperation::RemoveOneWithOpts(const BsonDocument& selector,
										   const BsonDocument* opts,
										   MongoError* error) {
	if (!impl_ || !impl_->bulk) return false;
	return mongoc_bulk_operation_remove_one_with_opts(
		impl_->bulk,
		static_cast<const bson_t*>(selector.RawBson()),
		opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
		error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoBulkOperation::RemoveManyWithOpts(const BsonDocument& selector,
											const BsonDocument* opts,
											MongoError* error) {
	if (!impl_ || !impl_->bulk) return false;
	return mongoc_bulk_operation_remove_many_with_opts(
		impl_->bulk,
		static_cast<const bson_t*>(selector.RawBson()),
		opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
		error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoBulkOperation::ReplaceOneWithOpts(const BsonDocument& selector,
											const BsonDocument& document,
											const BsonDocument* opts,
											MongoError* error) {
	if (!impl_ || !impl_->bulk) return false;
	return mongoc_bulk_operation_replace_one_with_opts(
		impl_->bulk,
		static_cast<const bson_t*>(selector.RawBson()),
		static_cast<const bson_t*>(document.RawBson()),
		opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
		error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoBulkOperation::UpdateOneWithOpts(const BsonDocument& selector,
										   const BsonDocument& document,
										   const BsonDocument* opts,
										   MongoError* error) {
	if (!impl_ || !impl_->bulk) return false;
	return mongoc_bulk_operation_update_one_with_opts(
		impl_->bulk,
		static_cast<const bson_t*>(selector.RawBson()),
		static_cast<const bson_t*>(document.RawBson()),
		opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
		error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

bool MongoBulkOperation::UpdateManyWithOpts(const BsonDocument& selector,
											const BsonDocument& document,
											const BsonDocument* opts,
											MongoError* error) {
	if (!impl_ || !impl_->bulk) return false;
	return mongoc_bulk_operation_update_many_with_opts(
		impl_->bulk,
		static_cast<const bson_t*>(selector.RawBson()),
		static_cast<const bson_t*>(document.RawBson()),
		opts ? static_cast<const bson_t*>(opts->RawBson()) : nullptr,
		error ? static_cast<bson_error_t*>(error->RawError()) : nullptr);
}

uint32_t MongoBulkOperation::Execute(BsonDocument* reply, MongoError* error) {
	if (!impl_ || !impl_->bulk) return 0;
	return mongoc_bulk_operation_execute(impl_->bulk,
										 reply ? static_cast<bson_t*>(reply->RawBson()) : nullptr,
										 error ? static_cast<bson_error_t*>(error->RawError())
											   : nullptr);
}

void MongoBulkOperation::SetBypassDocumentValidation(bool bypass) {
	if (impl_ && impl_->bulk)
		mongoc_bulk_operation_set_bypass_document_validation(impl_->bulk, bypass);
}

void MongoBulkOperation::SetLet(const BsonDocument& let) {
	if (impl_ && impl_->bulk)
		mongoc_bulk_operation_set_let(impl_->bulk, static_cast<const bson_t*>(let.RawBson()));
}

void MongoBulkOperation::SetWriteConcern(const MongoWriteConcern& write_concern) {
	if (impl_ && impl_->bulk)
		mongoc_bulk_operation_set_write_concern(
			impl_->bulk,
			static_cast<const mongoc_write_concern_t*>(write_concern.RawWriteConcern()));
}

void MongoBulkOperation::SetServerId(uint32_t server_id) {
	if (impl_ && impl_->bulk) mongoc_bulk_operation_set_server_id(impl_->bulk, server_id);
}

uint32_t MongoBulkOperation::GetServerId() const {
	return impl_ && impl_->bulk ? mongoc_bulk_operation_get_server_id(impl_->bulk) : 0;
}

void MongoBulkOperation::SetDatabase(const char* database) {
	if (impl_ && impl_->bulk) mongoc_bulk_operation_set_database(impl_->bulk, database);
}

void MongoBulkOperation::SetCollection(const char* collection) {
	if (impl_ && impl_->bulk) mongoc_bulk_operation_set_collection(impl_->bulk, collection);
}

void MongoBulkOperation::SetComment(const void* comment) {
	if (impl_ && impl_->bulk)
		mongoc_bulk_operation_set_comment(impl_->bulk, static_cast<const bson_value_t*>(comment));
}

void MongoBulkOperation::SetClient(void* client) {
	if (impl_ && impl_->bulk)
		mongoc_bulk_operation_set_client(impl_->bulk, static_cast<mongoc_client_t*>(client));
}

void MongoBulkOperation::SetClientSession(void* session) {
	if (impl_ && impl_->bulk)
		mongoc_bulk_operation_set_client_session(impl_->bulk,
												 static_cast<mongoc_client_session_t*>(session));
}

const void* MongoBulkOperation::GetWriteConcern() const {
	return impl_ && impl_->bulk ? mongoc_bulk_operation_get_write_concern(impl_->bulk) : nullptr;
}

void* MongoBulkOperation::RawBulkOperation() {
	return impl_ ? impl_->bulk : nullptr;
}

void MongoBulkOperation::SetRawBulkOperation(void* bulk) {
	if (!impl_) return;
	if (impl_->bulk == static_cast<mongoc_bulk_operation_t*>(bulk)) return;
	if (impl_->bulk) mongoc_bulk_operation_destroy(impl_->bulk);
	impl_->bulk = static_cast<mongoc_bulk_operation_t*>(bulk);
}

}  // namespace mongo
}  // namespace engine

#endif
