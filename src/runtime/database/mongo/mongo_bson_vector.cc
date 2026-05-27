#if defined(ENGINE_MONGODB_ENABLED)

#include "runtime/database/mongo/mongo_bson_vector.h"

#include <bson/bson.h>

#include "runtime/database/mongo/mongo_error.h"

namespace engine {
namespace mongo {

// Ensure inline storage is large enough for all view types.
static_assert(sizeof(BsonVectorInt8ConstView) >= sizeof(bson_vector_int8_const_view_t),
			  "BsonVectorInt8ConstView storage too small");
static_assert(sizeof(BsonVectorInt8View) >= sizeof(bson_vector_int8_view_t),
			  "BsonVectorInt8View storage too small");
static_assert(sizeof(BsonVectorFloat32ConstView) >= sizeof(bson_vector_float32_const_view_t),
			  "BsonVectorFloat32ConstView storage too small");
static_assert(sizeof(BsonVectorFloat32View) >= sizeof(bson_vector_float32_view_t),
			  "BsonVectorFloat32View storage too small");
static_assert(sizeof(BsonVectorPackedBitConstView) >= sizeof(bson_vector_packed_bit_const_view_t),
			  "BsonVectorPackedBitConstView storage too small");
static_assert(sizeof(BsonVectorPackedBitView) >= sizeof(bson_vector_packed_bit_view_t),
			  "BsonVectorPackedBitView storage too small");

// Cast helpers for member and friend functions.
#define VC8C(p) reinterpret_cast<bson_vector_int8_const_view_t*>((p)->storage_)
#define CV8C(p) reinterpret_cast<const bson_vector_int8_const_view_t*>((p)->storage_)
#define VC8(p) reinterpret_cast<bson_vector_int8_view_t*>((p)->storage_)
#define CV8(p) reinterpret_cast<const bson_vector_int8_view_t*>((p)->storage_)
#define VF32C(p) reinterpret_cast<bson_vector_float32_const_view_t*>((p)->storage_)
#define CVF32C(p) reinterpret_cast<const bson_vector_float32_const_view_t*>((p)->storage_)
#define VF32(p) reinterpret_cast<bson_vector_float32_view_t*>((p)->storage_)
#define CVF32(p) reinterpret_cast<const bson_vector_float32_view_t*>((p)->storage_)
#define VPBC(p) reinterpret_cast<bson_vector_packed_bit_const_view_t*>((p)->storage_)
#define CVPBC(p) reinterpret_cast<const bson_vector_packed_bit_const_view_t*>((p)->storage_)
#define VPB(p) reinterpret_cast<bson_vector_packed_bit_view_t*>((p)->storage_)
#define CVPB(p) reinterpret_cast<const bson_vector_packed_bit_view_t*>((p)->storage_)

// ═══════════════════════════════════════════════════════════════════════
// BsonVectorInt8ConstView
// ═══════════════════════════════════════════════════════════════════════

BsonVectorInt8ConstView::BsonVectorInt8ConstView() {
	memset(storage_, 0, sizeof(storage_));
}

bool BsonVectorInt8ConstView::Init(const uint8_t* binary_data, uint32_t binary_data_len) {
	return bson_vector_int8_const_view_init(VC8C(this), binary_data, binary_data_len);
}

bool BsonVectorInt8ConstView::FromIter(const BsonIter& iter) {
	return bson_vector_int8_const_view_from_iter(VC8C(this),
												 static_cast<const bson_iter_t*>(iter.RawIter()));
}

const int8_t* BsonVectorInt8ConstView::Pointer() const {
	return bson_vector_int8_const_view_pointer(*CV8C(this));
}

size_t BsonVectorInt8ConstView::Length() const {
	return bson_vector_int8_const_view_length(*CV8C(this));
}

bool BsonVectorInt8ConstView::Read(int8_t* values_out,
								   size_t element_count,
								   size_t vector_offset_elements) const {
	return bson_vector_int8_const_view_read(
		*CV8C(this), values_out, element_count, vector_offset_elements);
}

uint32_t BsonVectorInt8ConstView::BinaryDataLength(size_t element_count) {
	return bson_vector_int8_binary_data_length(element_count);
}

// ═══════════════════════════════════════════════════════════════════════
// BsonVectorInt8View
// ═══════════════════════════════════════════════════════════════════════

BsonVectorInt8View::BsonVectorInt8View() {
	memset(storage_, 0, sizeof(storage_));
}

bool BsonVectorInt8View::Init(uint8_t* binary_data, uint32_t binary_data_len) {
	return bson_vector_int8_view_init(VC8(this), binary_data, binary_data_len);
}

bool BsonVectorInt8View::FromIter(BsonIter& iter) {
	return bson_vector_int8_view_from_iter(VC8(this), static_cast<bson_iter_t*>(iter.RawIter()));
}

int8_t* BsonVectorInt8View::Pointer() {
	return bson_vector_int8_view_pointer(*CV8(this));
}

const int8_t* BsonVectorInt8View::ConstPointer() const {
	return bson_vector_int8_const_view_pointer(bson_vector_int8_view_as_const(*CV8(this)));
}

size_t BsonVectorInt8View::Length() const {
	return bson_vector_int8_view_length(*CV8(this));
}

bool BsonVectorInt8View::Read(int8_t* values_out,
							  size_t element_count,
							  size_t vector_offset_elements) const {
	return bson_vector_int8_view_read(
		*CV8(this), values_out, element_count, vector_offset_elements);
}

bool BsonVectorInt8View::Write(const int8_t* values,
							   size_t element_count,
							   size_t vector_offset_elements) {
	return bson_vector_int8_view_write(*CV8(this), values, element_count, vector_offset_elements);
}

BsonVectorInt8ConstView BsonVectorInt8View::AsConst() const {
	BsonVectorInt8ConstView result;
	*VC8C(&result) = bson_vector_int8_view_as_const(*CV8(this));
	return result;
}

uint32_t BsonVectorInt8View::BinaryDataLength(size_t element_count) {
	return bson_vector_int8_binary_data_length(element_count);
}

// ═══════════════════════════════════════════════════════════════════════
// BsonVectorFloat32ConstView
// ═══════════════════════════════════════════════════════════════════════

BsonVectorFloat32ConstView::BsonVectorFloat32ConstView() {
	memset(storage_, 0, sizeof(storage_));
}

bool BsonVectorFloat32ConstView::Init(const uint8_t* binary_data, uint32_t binary_data_len) {
	return bson_vector_float32_const_view_init(VF32C(this), binary_data, binary_data_len);
}

bool BsonVectorFloat32ConstView::FromIter(const BsonIter& iter) {
	return bson_vector_float32_const_view_from_iter(
		VF32C(this), static_cast<const bson_iter_t*>(iter.RawIter()));
}

size_t BsonVectorFloat32ConstView::Length() const {
	return bson_vector_float32_const_view_length(*CVF32C(this));
}

bool BsonVectorFloat32ConstView::Read(float* values_out,
									  size_t element_count,
									  size_t vector_offset_elements) const {
	return bson_vector_float32_const_view_read(
		*CVF32C(this), values_out, element_count, vector_offset_elements);
}

uint32_t BsonVectorFloat32ConstView::BinaryDataLength(size_t element_count) {
	return bson_vector_float32_binary_data_length(element_count);
}

// ═══════════════════════════════════════════════════════════════════════
// BsonVectorFloat32View
// ═══════════════════════════════════════════════════════════════════════

BsonVectorFloat32View::BsonVectorFloat32View() {
	memset(storage_, 0, sizeof(storage_));
}

bool BsonVectorFloat32View::Init(uint8_t* binary_data, uint32_t binary_data_len) {
	return bson_vector_float32_view_init(VF32(this), binary_data, binary_data_len);
}

bool BsonVectorFloat32View::FromIter(BsonIter& iter) {
	return bson_vector_float32_view_from_iter(VF32(this),
											  static_cast<bson_iter_t*>(iter.RawIter()));
}

size_t BsonVectorFloat32View::Length() const {
	return bson_vector_float32_view_length(*CVF32(this));
}

bool BsonVectorFloat32View::Read(float* values_out,
								 size_t element_count,
								 size_t vector_offset_elements) const {
	return bson_vector_float32_view_read(
		*CVF32(this), values_out, element_count, vector_offset_elements);
}

bool BsonVectorFloat32View::Write(const float* values,
								  size_t element_count,
								  size_t vector_offset_elements) {
	return bson_vector_float32_view_write(
		*CVF32(this), values, element_count, vector_offset_elements);
}

BsonVectorFloat32ConstView BsonVectorFloat32View::AsConst() const {
	BsonVectorFloat32ConstView result;
	*VF32C(&result) = bson_vector_float32_view_as_const(*CVF32(this));
	return result;
}

uint32_t BsonVectorFloat32View::BinaryDataLength(size_t element_count) {
	return bson_vector_float32_binary_data_length(element_count);
}

// ═══════════════════════════════════════════════════════════════════════
// BsonVectorPackedBitConstView
// ═══════════════════════════════════════════════════════════════════════

BsonVectorPackedBitConstView::BsonVectorPackedBitConstView() {
	memset(storage_, 0, sizeof(storage_));
}

bool BsonVectorPackedBitConstView::Init(const uint8_t* binary_data, uint32_t binary_data_len) {
	return bson_vector_packed_bit_const_view_init(VPBC(this), binary_data, binary_data_len);
}

bool BsonVectorPackedBitConstView::FromIter(const BsonIter& iter) {
	return bson_vector_packed_bit_const_view_from_iter(
		VPBC(this), static_cast<const bson_iter_t*>(iter.RawIter()));
}

size_t BsonVectorPackedBitConstView::Length() const {
	return bson_vector_packed_bit_const_view_length(*CVPBC(this));
}

size_t BsonVectorPackedBitConstView::LengthBytes() const {
	return bson_vector_packed_bit_const_view_length_bytes(*CVPBC(this));
}

size_t BsonVectorPackedBitConstView::Padding() const {
	return bson_vector_packed_bit_const_view_padding(*CVPBC(this));
}

bool BsonVectorPackedBitConstView::ReadPacked(uint8_t* packed_values_out,
											  size_t byte_count,
											  size_t vector_offset_bytes) const {
	return bson_vector_packed_bit_const_view_read_packed(
		*CVPBC(this), packed_values_out, byte_count, vector_offset_bytes);
}

bool BsonVectorPackedBitConstView::UnpackBool(bool* unpacked_values_out,
											  size_t element_count,
											  size_t vector_offset_elements) const {
	return bson_vector_packed_bit_const_view_unpack_bool(
		*CVPBC(this), unpacked_values_out, element_count, vector_offset_elements);
}

uint32_t BsonVectorPackedBitConstView::BinaryDataLength(size_t element_count) {
	return bson_vector_packed_bit_binary_data_length(element_count);
}

// ═══════════════════════════════════════════════════════════════════════
// BsonVectorPackedBitView
// ═══════════════════════════════════════════════════════════════════════

BsonVectorPackedBitView::BsonVectorPackedBitView() {
	memset(storage_, 0, sizeof(storage_));
}

bool BsonVectorPackedBitView::Init(uint8_t* binary_data, uint32_t binary_data_len) {
	return bson_vector_packed_bit_view_init(VPB(this), binary_data, binary_data_len);
}

bool BsonVectorPackedBitView::FromIter(BsonIter& iter) {
	return bson_vector_packed_bit_view_from_iter(VPB(this),
												 static_cast<bson_iter_t*>(iter.RawIter()));
}

size_t BsonVectorPackedBitView::Length() const {
	return bson_vector_packed_bit_view_length(*CVPB(this));
}

size_t BsonVectorPackedBitView::LengthBytes() const {
	return bson_vector_packed_bit_view_length_bytes(*CVPB(this));
}

size_t BsonVectorPackedBitView::Padding() const {
	return bson_vector_packed_bit_view_padding(*CVPB(this));
}

bool BsonVectorPackedBitView::ReadPacked(uint8_t* packed_values_out,
										 size_t byte_count,
										 size_t vector_offset_bytes) const {
	return bson_vector_packed_bit_view_read_packed(
		*CVPB(this), packed_values_out, byte_count, vector_offset_bytes);
}

bool BsonVectorPackedBitView::UnpackBool(bool* unpacked_values_out,
										 size_t element_count,
										 size_t vector_offset_elements) const {
	return bson_vector_packed_bit_view_unpack_bool(
		*CVPB(this), unpacked_values_out, element_count, vector_offset_elements);
}

bool BsonVectorPackedBitView::WritePacked(const uint8_t* packed_values,
										  size_t byte_count,
										  size_t vector_offset_bytes) {
	return bson_vector_packed_bit_view_write_packed(
		*CVPB(this), packed_values, byte_count, vector_offset_bytes);
}

bool BsonVectorPackedBitView::PackBool(const bool* unpacked_values,
									   size_t element_count,
									   size_t vector_offset_elements) {
	return bson_vector_packed_bit_view_pack_bool(
		*CVPB(this), unpacked_values, element_count, vector_offset_elements);
}

BsonVectorPackedBitConstView BsonVectorPackedBitView::AsConst() const {
	BsonVectorPackedBitConstView result;
	*VPBC(&result) = bson_vector_packed_bit_view_as_const(*CVPB(this));
	return result;
}

uint32_t BsonVectorPackedBitView::BinaryDataLength(size_t element_count) {
	return bson_vector_packed_bit_binary_data_length(element_count);
}

// ═══════════════════════════════════════════════════════════════════════
// BsonDocument vector append helpers
// ═══════════════════════════════════════════════════════════════════════

bool BsonAppendVectorInt8Uninit(BsonDocument& doc,
								const char* key,
								size_t element_count,
								BsonVectorInt8View& view_out) {
	return bson_append_vector_int8_uninit(
		static_cast<bson_t*>(doc.RawBson()), key, -1, element_count, VC8(&view_out));
}

bool BsonAppendVectorFloat32Uninit(BsonDocument& doc,
								   const char* key,
								   size_t element_count,
								   BsonVectorFloat32View& view_out) {
	return bson_append_vector_float32_uninit(
		static_cast<bson_t*>(doc.RawBson()), key, -1, element_count, VF32(&view_out));
}

bool BsonAppendVectorPackedBitUninit(BsonDocument& doc,
									 const char* key,
									 size_t element_count,
									 BsonVectorPackedBitView& view_out) {
	return bson_append_vector_packed_bit_uninit(
		static_cast<bson_t*>(doc.RawBson()), key, -1, element_count, VPB(&view_out));
}

bool BsonAppendVectorInt8FromArray(BsonDocument& doc,
								   const char* key,
								   const BsonIter& iter,
								   MongoError* error) {
	bson_error_t err;
	return bson_append_vector_int8_from_array(static_cast<bson_t*>(doc.RawBson()),
											  key,
											  -1,
											  static_cast<const bson_iter_t*>(iter.RawIter()),
											  error ? static_cast<bson_error_t*>(error->RawError())
													: &err);
}

bool BsonAppendVectorFloat32FromArray(BsonDocument& doc,
									  const char* key,
									  const BsonIter& iter,
									  MongoError* error) {
	bson_error_t err;
	return bson_append_vector_float32_from_array(
		static_cast<bson_t*>(doc.RawBson()),
		key,
		-1,
		static_cast<const bson_iter_t*>(iter.RawIter()),
		error ? static_cast<bson_error_t*>(error->RawError()) : &err);
}

bool BsonAppendVectorPackedBitFromArray(BsonDocument& doc,
										const char* key,
										const BsonIter& iter,
										MongoError* error) {
	bson_error_t err;
	return bson_append_vector_packed_bit_from_array(
		static_cast<bson_t*>(doc.RawBson()),
		key,
		-1,
		static_cast<const bson_iter_t*>(iter.RawIter()),
		error ? static_cast<bson_error_t*>(error->RawError()) : &err);
}

bool BsonAppendArrayFromVectorInt8(BsonDocument& doc,
								   const char* key,
								   const BsonVectorInt8ConstView& view) {
	return bson_append_array_from_vector_int8(
		static_cast<bson_t*>(doc.RawBson()), key, -1, *CV8C(&view));
}

bool BsonAppendArrayFromVectorFloat32(BsonDocument& doc,
									  const char* key,
									  const BsonVectorFloat32ConstView& view) {
	return bson_append_array_from_vector_float32(
		static_cast<bson_t*>(doc.RawBson()), key, -1, *CVF32C(&view));
}

bool BsonAppendArrayFromVectorPackedBit(BsonDocument& doc,
										const char* key,
										const BsonVectorPackedBitConstView& view) {
	return bson_append_array_from_vector_packed_bit(
		static_cast<bson_t*>(doc.RawBson()), key, -1, *CVPBC(&view));
}

// ═══════════════════════════════════════════════════════════════════════
// BsonArrayBuilder vector append helpers
// ═══════════════════════════════════════════════════════════════════════

bool BsonArrayBuilderAppendVectorInt8Elements(BsonArrayBuilder& builder,
											  const BsonVectorInt8ConstView& view) {
	return bson_array_builder_append_vector_int8_elements(
		static_cast<bson_array_builder_t*>(builder.Raw()), *CV8C(&view));
}

bool BsonArrayBuilderAppendVectorFloat32Elements(BsonArrayBuilder& builder,
												 const BsonVectorFloat32ConstView& view) {
	return bson_array_builder_append_vector_float32_elements(
		static_cast<bson_array_builder_t*>(builder.Raw()), *CVF32C(&view));
}

bool BsonArrayBuilderAppendVectorPackedBitElements(BsonArrayBuilder& builder,
												   const BsonVectorPackedBitConstView& view) {
	return bson_array_builder_append_vector_packed_bit_elements(
		static_cast<bson_array_builder_t*>(builder.Raw()), *CVPBC(&view));
}

bool BsonArrayBuilderAppendVectorElements(BsonArrayBuilder& builder, const BsonIter& iter) {
	return bson_array_builder_append_vector_elements(
		static_cast<bson_array_builder_t*>(builder.Raw()),
		static_cast<const bson_iter_t*>(iter.RawIter()));
}

}  // namespace mongo
}  // namespace engine

#endif
