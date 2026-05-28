#pragma once

#if defined(ENGINE_MONGODB_ENABLED)


#include <cstddef>
#include <cstdint>

#include "runtime/core/engine_api.h"
#include "runtime/database/mongo/mongo_bson.h"

namespace engine {
namespace mongo {

// BSON Vector constants

constexpr size_t kBsonVectorHeaderLen = 2;

// Forward declarations for view classes.
class BsonVectorInt8ConstView;
class BsonVectorInt8View;
class BsonVectorFloat32ConstView;
class BsonVectorFloat32View;
class BsonVectorPackedBitConstView;
class BsonVectorPackedBitView;

// BsonDocument vector append helpers (free functions)

ENGINE_API bool BsonAppendVectorInt8Uninit(BsonDocument& doc,
										   const char* key,
										   size_t element_count,
										   BsonVectorInt8View& view_out);
ENGINE_API bool BsonAppendVectorFloat32Uninit(BsonDocument& doc,
											  const char* key,
											  size_t element_count,
											  BsonVectorFloat32View& view_out);
ENGINE_API bool BsonAppendVectorPackedBitUninit(BsonDocument& doc,
												const char* key,
												size_t element_count,
												BsonVectorPackedBitView& view_out);

ENGINE_API bool BsonAppendVectorInt8FromArray(BsonDocument& doc,
											  const char* key,
											  const BsonIter& iter,
											  MongoError* error = nullptr);
ENGINE_API bool BsonAppendVectorFloat32FromArray(BsonDocument& doc,
												 const char* key,
												 const BsonIter& iter,
												 MongoError* error = nullptr);
ENGINE_API bool BsonAppendVectorPackedBitFromArray(BsonDocument& doc,
												   const char* key,
												   const BsonIter& iter,
												   MongoError* error = nullptr);

ENGINE_API bool BsonAppendArrayFromVectorInt8(BsonDocument& doc,
											  const char* key,
											  const BsonVectorInt8ConstView& view);
ENGINE_API bool BsonAppendArrayFromVectorFloat32(BsonDocument& doc,
												 const char* key,
												 const BsonVectorFloat32ConstView& view);
ENGINE_API bool BsonAppendArrayFromVectorPackedBit(BsonDocument& doc,
												   const char* key,
												   const BsonVectorPackedBitConstView& view);

// BsonArrayBuilder vector append helpers (free functions)

ENGINE_API bool BsonArrayBuilderAppendVectorInt8Elements(BsonArrayBuilder& builder,
														 const BsonVectorInt8ConstView& view);
ENGINE_API bool BsonArrayBuilderAppendVectorFloat32Elements(BsonArrayBuilder& builder,
															const BsonVectorFloat32ConstView& view);
ENGINE_API bool BsonArrayBuilderAppendVectorPackedBitElements(
	BsonArrayBuilder& builder, const BsonVectorPackedBitConstView& view);
ENGINE_API bool BsonArrayBuilderAppendVectorElements(BsonArrayBuilder& builder,
													 const BsonIter& iter);

// BsonVectorInt8ConstView

class ENGINE_API BsonVectorInt8ConstView {
	public:
	BsonVectorInt8ConstView();

	bool Init(const uint8_t* binary_data, uint32_t binary_data_len);
	bool FromIter(const BsonIter& iter);

	const int8_t* Pointer() const;
	size_t Length() const;
	bool Read(int8_t* values_out, size_t element_count, size_t vector_offset_elements = 0) const;

	static uint32_t BinaryDataLength(size_t element_count);

	private:
	friend class BsonVectorInt8View;
	friend bool BsonAppendArrayFromVectorInt8(BsonDocument&,
											  const char*,
											  const BsonVectorInt8ConstView&);
	friend bool BsonArrayBuilderAppendVectorInt8Elements(BsonArrayBuilder&,
														 const BsonVectorInt8ConstView&);
	alignas(8) char storage_[24];
};

// BsonVectorInt8View

class ENGINE_API BsonVectorInt8View {
	public:
	BsonVectorInt8View();

	bool Init(uint8_t* binary_data, uint32_t binary_data_len);
	bool FromIter(BsonIter& iter);

	int8_t* Pointer();
	const int8_t* ConstPointer() const;
	size_t Length() const;
	bool Read(int8_t* values_out, size_t element_count, size_t vector_offset_elements = 0) const;
	bool Write(const int8_t* values, size_t element_count, size_t vector_offset_elements = 0);

	BsonVectorInt8ConstView AsConst() const;

	static uint32_t BinaryDataLength(size_t element_count);

	private:
	friend bool BsonAppendVectorInt8Uninit(BsonDocument&, const char*, size_t, BsonVectorInt8View&);
	alignas(8) char storage_[24];
};

// BsonVectorFloat32ConstView

class ENGINE_API BsonVectorFloat32ConstView {
	public:
	BsonVectorFloat32ConstView();

	bool Init(const uint8_t* binary_data, uint32_t binary_data_len);
	bool FromIter(const BsonIter& iter);

	size_t Length() const;
	bool Read(float* values_out, size_t element_count, size_t vector_offset_elements = 0) const;

	static uint32_t BinaryDataLength(size_t element_count);

	private:
	friend class BsonVectorFloat32View;
	friend bool BsonAppendArrayFromVectorFloat32(BsonDocument&,
												 const char*,
												 const BsonVectorFloat32ConstView&);
	friend bool BsonArrayBuilderAppendVectorFloat32Elements(BsonArrayBuilder&,
															const BsonVectorFloat32ConstView&);
	alignas(8) char storage_[24];
};

// BsonVectorFloat32View

class ENGINE_API BsonVectorFloat32View {
	public:
	BsonVectorFloat32View();

	bool Init(uint8_t* binary_data, uint32_t binary_data_len);
	bool FromIter(BsonIter& iter);

	size_t Length() const;
	bool Read(float* values_out, size_t element_count, size_t vector_offset_elements = 0) const;
	bool Write(const float* values, size_t element_count, size_t vector_offset_elements = 0);

	BsonVectorFloat32ConstView AsConst() const;

	static uint32_t BinaryDataLength(size_t element_count);

	private:
	friend bool BsonAppendVectorFloat32Uninit(BsonDocument&,
											  const char*,
											  size_t,
											  BsonVectorFloat32View&);
	alignas(8) char storage_[24];
};

// BsonVectorPackedBitConstView

class ENGINE_API BsonVectorPackedBitConstView {
	public:
	BsonVectorPackedBitConstView();

	bool Init(const uint8_t* binary_data, uint32_t binary_data_len);
	bool FromIter(const BsonIter& iter);

	size_t Length() const;
	size_t LengthBytes() const;
	size_t Padding() const;
	bool ReadPacked(uint8_t* packed_values_out,
					size_t byte_count,
					size_t vector_offset_bytes = 0) const;
	bool UnpackBool(bool* unpacked_values_out,
					size_t element_count,
					size_t vector_offset_elements = 0) const;

	static uint32_t BinaryDataLength(size_t element_count);

	private:
	friend class BsonVectorPackedBitView;
	friend bool BsonAppendArrayFromVectorPackedBit(BsonDocument&,
												   const char*,
												   const BsonVectorPackedBitConstView&);
	friend bool BsonArrayBuilderAppendVectorPackedBitElements(BsonArrayBuilder&,
															  const BsonVectorPackedBitConstView&);
	alignas(8) char storage_[24];
};

// BsonVectorPackedBitView

class ENGINE_API BsonVectorPackedBitView {
	public:
	BsonVectorPackedBitView();

	bool Init(uint8_t* binary_data, uint32_t binary_data_len);
	bool FromIter(BsonIter& iter);

	size_t Length() const;
	size_t LengthBytes() const;
	size_t Padding() const;
	bool ReadPacked(uint8_t* packed_values_out,
					size_t byte_count,
					size_t vector_offset_bytes = 0) const;
	bool UnpackBool(bool* unpacked_values_out,
					size_t element_count,
					size_t vector_offset_elements = 0) const;
	bool WritePacked(const uint8_t* packed_values,
					 size_t byte_count,
					 size_t vector_offset_bytes = 0);
	bool PackBool(const bool* unpacked_values,
				  size_t element_count,
				  size_t vector_offset_elements = 0);

	BsonVectorPackedBitConstView AsConst() const;

	static uint32_t BinaryDataLength(size_t element_count);

	private:
	friend bool BsonAppendVectorPackedBitUninit(BsonDocument&,
												const char*,
												size_t,
												BsonVectorPackedBitView&);
	alignas(8) char storage_[24];
};

}  // namespace mongo
}  // namespace engine

#endif
