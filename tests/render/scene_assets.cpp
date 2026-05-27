#include "scene_assets.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace CullingEngineExample {
namespace {
constexpr const char* kModelsCategory = "models";
constexpr const char* kScenesCategory = "scenes";

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct JsonValue {
  enum class Type { kNull, kBoolean, kNumber, kString, kArray, kObject };

  Type type = Type::kNull;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<JsonValue> array;
  std::map<std::string, JsonValue> object;
};

class JsonParser {
 public:
  explicit JsonParser(std::string_view text) : mText(text) {}

  JsonValue Parse() {
    JsonValue value = ParseValue();
    SkipWhitespace();
    if (mOffset != mText.size()) {
      Fail("unexpected trailing characters");
    }
    return value;
  }

 private:
  void SkipWhitespace() {
    while (mOffset < mText.size() &&
           std::isspace(static_cast<unsigned char>(mText[mOffset])) != 0) {
      ++mOffset;
    }
  }

  [[noreturn]] void Fail(const std::string& message) const {
    throw std::runtime_error("JSON parse error at byte " +
                             std::to_string(mOffset) + ": " + message);
  }

  bool Consume(char expected) {
    SkipWhitespace();
    if (mOffset < mText.size() && mText[mOffset] == expected) {
      ++mOffset;
      return true;
    }
    return false;
  }

  void Expect(char expected) {
    if (!Consume(expected)) {
      Fail(std::string("expected '") + expected + "'");
    }
  }

  JsonValue ParseValue() {
    SkipWhitespace();
    if (mOffset >= mText.size()) {
      Fail("unexpected end of input");
    }

    const char c = mText[mOffset];
    if (c == '{') {
      return ParseObject();
    }
    if (c == '[') {
      return ParseArray();
    }
    if (c == '"') {
      JsonValue value;
      value.type = JsonValue::Type::kString;
      value.string = ParseString();
      return value;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      return ParseNumber();
    }
    if (StartsWith("true")) {
      mOffset += 4;
      JsonValue value;
      value.type = JsonValue::Type::kBoolean;
      value.boolean = true;
      return value;
    }
    if (StartsWith("false")) {
      mOffset += 5;
      JsonValue value;
      value.type = JsonValue::Type::kBoolean;
      value.boolean = false;
      return value;
    }
    if (StartsWith("null")) {
      mOffset += 4;
      return JsonValue{};
    }

    Fail("expected a JSON value");
  }

  JsonValue ParseObject() {
    Expect('{');
    JsonValue value;
    value.type = JsonValue::Type::kObject;

    if (Consume('}')) {
      return value;
    }

    while (true) {
      SkipWhitespace();
      if (mOffset >= mText.size() || mText[mOffset] != '"') {
        Fail("expected an object key string");
      }

      std::string key = ParseString();
      Expect(':');
      value.object.emplace(std::move(key), ParseValue());

      if (Consume('}')) {
        return value;
      }
      Expect(',');
    }
  }

  JsonValue ParseArray() {
    Expect('[');
    JsonValue value;
    value.type = JsonValue::Type::kArray;

    if (Consume(']')) {
      return value;
    }

    while (true) {
      value.array.push_back(ParseValue());
      if (Consume(']')) {
        return value;
      }
      Expect(',');
    }
  }

  std::string ParseString() {
    Expect('"');
    std::string result;

    while (mOffset < mText.size()) {
      const char c = mText[mOffset++];
      if (c == '"') {
        return result;
      }
      if (static_cast<unsigned char>(c) < 0x20u) {
        Fail("unescaped control character in string");
      }
      if (c != '\\') {
        result.push_back(c);
        continue;
      }

      if (mOffset >= mText.size()) {
        Fail("unterminated string escape");
      }

      const char escape = mText[mOffset++];
      switch (escape) {
        case '"':
        case '\\':
        case '/':
          result.push_back(escape);
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'u':
          result += ParseUnicodeEscapeAsUtf8();
          break;
        default:
          Fail("unsupported string escape");
      }
    }

    Fail("unterminated string");
  }

  std::string ParseUnicodeEscapeAsUtf8() {
    if (mOffset + 4 > mText.size()) {
      Fail("short unicode escape");
    }

    unsigned int codePoint = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = mText[mOffset++];
      codePoint <<= 4;
      if (c >= '0' && c <= '9') {
        codePoint += static_cast<unsigned int>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        codePoint += static_cast<unsigned int>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        codePoint += static_cast<unsigned int>(c - 'A' + 10);
      } else {
        Fail("invalid unicode escape");
      }
    }

    std::string result;
    if (codePoint <= 0x7Fu) {
      result.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FFu) {
      result.push_back(static_cast<char>(0xC0u | (codePoint >> 6)));
      result.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else {
      result.push_back(static_cast<char>(0xE0u | (codePoint >> 12)));
      result.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
      result.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    }
    return result;
  }

  JsonValue ParseNumber() {
    const std::size_t start = mOffset;
    if (mText[mOffset] == '-') {
      ++mOffset;
    }

    if (mOffset >= mText.size()) {
      Fail("incomplete number");
    }
    if (mText[mOffset] == '0') {
      ++mOffset;
    } else if (mText[mOffset] >= '1' && mText[mOffset] <= '9') {
      while (mOffset < mText.size() && mText[mOffset] >= '0' &&
             mText[mOffset] <= '9') {
        ++mOffset;
      }
    } else {
      Fail("invalid number");
    }

    if (mOffset < mText.size() && mText[mOffset] == '.') {
      ++mOffset;
      const std::size_t fractionStart = mOffset;
      while (mOffset < mText.size() && mText[mOffset] >= '0' &&
             mText[mOffset] <= '9') {
        ++mOffset;
      }
      if (fractionStart == mOffset) {
        Fail("number fraction has no digits");
      }
    }

    if (mOffset < mText.size() &&
        (mText[mOffset] == 'e' || mText[mOffset] == 'E')) {
      ++mOffset;
      if (mOffset < mText.size() &&
          (mText[mOffset] == '+' || mText[mOffset] == '-')) {
        ++mOffset;
      }
      const std::size_t exponentStart = mOffset;
      while (mOffset < mText.size() && mText[mOffset] >= '0' &&
             mText[mOffset] <= '9') {
        ++mOffset;
      }
      if (exponentStart == mOffset) {
        Fail("number exponent has no digits");
      }
    }

    const std::string numberText(mText.substr(start, mOffset - start));
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(numberText.c_str(), &end);
    if (errno == ERANGE || end == nullptr || *end != '\0') {
      Fail("invalid number");
    }

    JsonValue value;
    value.type = JsonValue::Type::kNumber;
    value.number = parsed;
    return value;
  }

  bool StartsWith(std::string_view literal) const {
    return mText.substr(mOffset, literal.size()) == literal;
  }

  std::string_view mText;
  std::size_t mOffset = 0;
};

std::string Trim(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1);
  }
  return std::string(value);
}

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open file: " + path.string());
  }

  std::ostringstream stream;
  stream << input.rdbuf();
  return stream.str();
}

std::string JsonTypeName(JsonValue::Type type) {
  switch (type) {
    case JsonValue::Type::kNull:
      return "null";
    case JsonValue::Type::kBoolean:
      return "boolean";
    case JsonValue::Type::kNumber:
      return "number";
    case JsonValue::Type::kString:
      return "string";
    case JsonValue::Type::kArray:
      return "array";
    case JsonValue::Type::kObject:
      return "object";
  }
  return "unknown";
}

void RequireType(const JsonValue& value, JsonValue::Type expected,
                 std::string_view context) {
  if (value.type != expected) {
    throw std::runtime_error(std::string(context) + " must be a " +
                             JsonTypeName(expected) + ", got " +
                             JsonTypeName(value.type));
  }
}

const JsonValue* FindMember(const JsonValue& object, std::string_view key) {
  if (object.type != JsonValue::Type::kObject) {
    return nullptr;
  }

  const auto it = object.object.find(std::string(key));
  if (it == object.object.end()) {
    return nullptr;
  }
  return &it->second;
}

const JsonValue& RequireMember(const JsonValue& object, std::string_view key,
                               std::string_view context) {
  RequireType(object, JsonValue::Type::kObject, context);
  const JsonValue* value = FindMember(object, key);
  if (value == nullptr) {
    throw std::runtime_error(std::string(context) +
                             " is missing required member '" +
                             std::string(key) + "'");
  }
  return *value;
}

std::string JsonString(const JsonValue& value, std::string_view context) {
  RequireType(value, JsonValue::Type::kString, context);
  return value.string;
}

bool JsonBool(const JsonValue& value, std::string_view context) {
  RequireType(value, JsonValue::Type::kBoolean, context);
  return value.boolean;
}

float JsonFloat(const JsonValue& value, std::string_view context) {
  RequireType(value, JsonValue::Type::kNumber, context);
  if (!std::isfinite(value.number) ||
      value.number < -static_cast<double>(std::numeric_limits<float>::max()) ||
      value.number > static_cast<double>(std::numeric_limits<float>::max())) {
    throw std::runtime_error(std::string(context) +
                             " is outside the float range");
  }
  return static_cast<float>(value.number);
}

unsigned int JsonUnsigned(const JsonValue& value, std::string_view context) {
  RequireType(value, JsonValue::Type::kNumber, context);
  if (!std::isfinite(value.number) || value.number < 0.0 ||
      value.number >
          static_cast<double>(std::numeric_limits<unsigned int>::max()) ||
      std::floor(value.number) != value.number) {
    throw std::runtime_error(std::string(context) +
                             " must be a non-negative integer");
  }
  return static_cast<unsigned int>(value.number);
}

std::string OptionalJsonString(const JsonValue& object, std::string_view key,
                               const std::string& fallback,
                               std::string_view context) {
  const JsonValue* value = FindMember(object, key);
  if (value == nullptr) {
    return fallback;
  }
  return JsonString(*value, std::string(context) + "." + std::string(key));
}

bool OptionalJsonBool(const JsonValue& object, std::string_view key,
                      bool fallback, std::string_view context) {
  const JsonValue* value = FindMember(object, key);
  if (value == nullptr) {
    return fallback;
  }
  return JsonBool(*value, std::string(context) + "." + std::string(key));
}

float OptionalJsonFloat(const JsonValue& object, std::string_view key,
                        float fallback, std::string_view context) {
  const JsonValue* value = FindMember(object, key);
  if (value == nullptr) {
    return fallback;
  }
  return JsonFloat(*value, std::string(context) + "." + std::string(key));
}

unsigned int OptionalJsonUnsigned(const JsonValue& object, std::string_view key,
                                  unsigned int fallback,
                                  std::string_view context) {
  const JsonValue* value = FindMember(object, key);
  if (value == nullptr) {
    return fallback;
  }
  return JsonUnsigned(*value, std::string(context) + "." + std::string(key));
}

template <std::size_t Size>
std::array<float, Size> JsonFloatArray(const JsonValue& value,
                                       std::string_view context) {
  RequireType(value, JsonValue::Type::kArray, context);
  if (value.array.size() != Size) {
    throw std::runtime_error(std::string(context) + " must contain exactly " +
                             std::to_string(Size) + " numbers");
  }

  std::array<float, Size> result{};
  for (std::size_t i = 0; i < Size; ++i) {
    result[i] = JsonFloat(value.array[i],
                          std::string(context) + "[" + std::to_string(i) + "]");
  }
  return result;
}

template <std::size_t Size>
std::array<float, Size> OptionalFloatArray(
    const JsonValue& object, std::string_view key,
    const std::array<float, Size>& fallback, std::string_view context) {
  const JsonValue* value = FindMember(object, key);
  if (value == nullptr) {
    return fallback;
  }
  return JsonFloatArray<Size>(*value,
                              std::string(context) + "." + std::string(key));
}

std::filesystem::path ResolveRelativePath(const std::filesystem::path& baseDir,
                                          const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) {
    return path;
  }
  return baseDir / path;
}

std::filesystem::path SceneBaseDir(const std::filesystem::path& scenePath) {
  const std::filesystem::path parent = scenePath.parent_path();
  return parent.empty() ? std::filesystem::path(".") : parent;
}

std::filesystem::path NormalizeRootPath(const std::filesystem::path& path) {
  std::error_code error;
  const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(path, error);
  if (!error) {
    return canonical;
  }

  error.clear();
  const std::filesystem::path absolute = std::filesystem::absolute(path, error);
  if (!error) {
    return absolute.lexically_normal();
  }
  return path.lexically_normal();
}

void ValidateCategorizedAssetPath(const std::filesystem::path& path,
                                  std::string_view context) {
  if (path.empty()) {
    throw std::runtime_error(std::string(context) + " must not be empty");
  }
  if (path.is_absolute()) {
    throw std::runtime_error(std::string(context) +
                             " must be relative to its asset category");
  }

  for (const auto& part : path) {
    if (part == "..") {
      throw std::runtime_error(std::string(context) +
                               " must not escape its asset category");
    }
  }
}

std::array<float, 16> MakeColumnMajorTransform(float tx, float ty, float tz,
                                               float scale, float yawRadians,
                                               float rollRadians) {
  const float cy = std::cos(yawRadians);
  const float sy = std::sin(yawRadians);
  const float cz = std::cos(rollRadians);
  const float sz = std::sin(rollRadians);

  const float r00 = cz * cy;
  const float r01 = -sz;
  const float r02 = cz * sy;

  const float r10 = sz * cy;
  const float r11 = cz;
  const float r12 = sz * sy;

  const float r20 = -sy;
  const float r21 = 0.0f;
  const float r22 = cy;

  return {r00 * scale, r10 * scale, r20 * scale, 0.0f,
          r01 * scale, r11 * scale, r21 * scale, 0.0f,
          r02 * scale, r12 * scale, r22 * scale, 0.0f,
          tx,          ty,          tz,          1.0f};
}

struct DefaultInstanceSpec {
  const char* name = "";
  const char* meshId = "";
  float tx = 0.0f;
  float ty = 0.0f;
  float tz = 0.0f;
  float scale = 1.0f;
  float yawRadians = 0.0f;
  float rollRadians = 0.0f;
};

const std::array<std::pair<const char*, const char*>, 7>& DefaultMeshSpecs() {
  static const std::array<std::pair<const char*, const char*>, 7> specs{
      std::pair<const char*, const char*>{"spot", "spot.obj"},
      std::pair<const char*, const char*>{"teapot", "teapot.obj"},
      std::pair<const char*, const char*>{"suzanne", "suzanne.obj"},
      std::pair<const char*, const char*>{"woody", "woody.obj"},
      std::pair<const char*, const char*>{"cube", "cube.obj"},
      std::pair<const char*, const char*>{"pyramid", "pyramid.obj"},
      std::pair<const char*, const char*>{"octahedron", "octahedron.obj"}};
  return specs;
}

const std::array<DefaultInstanceSpec, 7>& DefaultInstanceSpecs() {
  static const std::array<DefaultInstanceSpec, 7> specs{
      DefaultInstanceSpec{"spot-left", "spot", -0.52f, -0.18f, 0.20f, 0.72f,
                          0.55f, -0.10f},
      DefaultInstanceSpec{"teapot-right", "teapot", 0.42f, -0.20f, 0.00f, 0.58f,
                          -0.38f, 0.04f},
      DefaultInstanceSpec{"suzanne-top", "suzanne", -0.18f, 0.38f, -0.24f,
                          0.48f, 0.92f, 0.16f},
      DefaultInstanceSpec{"woody-top-right", "woody", 0.58f, 0.36f, 0.36f,
                          0.52f, -0.78f, -0.12f},
      DefaultInstanceSpec{"cube-bottom-left", "cube", -0.72f, -0.56f, 0.62f,
                          0.34f, 0.25f, -0.08f},
      DefaultInstanceSpec{"pyramid-bottom-center", "pyramid", 0.00f, -0.58f,
                          0.44f, 0.38f, -0.55f, 0.10f},
      DefaultInstanceSpec{"octahedron-bottom-right", "octahedron", 0.72f,
                          -0.54f, 0.58f, 0.32f, 0.65f, 0.30f}};
  return specs;
}

unsigned int RenderModeFromString(const std::string& value,
                                  std::string_view context) {
  if (value == "full") {
    return CULLING_ENGINE_RENDER_MODE_FULL;
  }
  if (value == "coherent") {
    return CULLING_ENGINE_RENDER_MODE_COHERENT;
  }
  if (value == "coherentFast") {
    return CULLING_ENGINE_RENDER_MODE_COHERENT_FAST;
  }
  if (value == "toggleRenderType") {
    return CULLING_ENGINE_RENDER_MODE_TOGGLE_RENDER_TYPE;
  }
  throw std::runtime_error(std::string(context) +
                           " has unsupported render mode: " + value);
}

RenderSettings ParseRenderSettings(const JsonValue& root) {
  RenderSettings render;
  const JsonValue* renderValue = FindMember(root, "render");
  if (renderValue == nullptr) {
    return render;
  }

  RequireType(*renderValue, JsonValue::Type::kObject, "scene.render");
  render.width =
      OptionalJsonUnsigned(*renderValue, "width", render.width, "scene.render");
  render.height = OptionalJsonUnsigned(*renderValue, "height", render.height,
                                       "scene.render");
  render.nearPlane = OptionalJsonFloat(*renderValue, "nearPlane",
                                       render.nearPlane, "scene.render");
  render.counterClockwise =
      OptionalJsonBool(*renderValue, "counterClockwise",
                       render.counterClockwise, "scene.render");
  render.visualizeDepth = OptionalJsonBool(
      *renderValue, "visualizeDepth", render.visualizeDepth, "scene.render");

  const std::string mode =
      OptionalJsonString(*renderValue, "mode", "full", "scene.render");
  render.renderMode = RenderModeFromString(mode, "scene.render.mode");
  return render;
}

Camera ParseCamera(const JsonValue& root) {
  Camera camera;

  const JsonValue* cameraValue = FindMember(root, "camera");
  if (cameraValue == nullptr) {
    return camera;
  }

  RequireType(*cameraValue, JsonValue::Type::kObject, "scene.camera");
  camera.position = OptionalFloatArray<3>(*cameraValue, "position",
                                          camera.position, "scene.camera");
  camera.direction = OptionalFloatArray<3>(*cameraValue, "direction",
                                           camera.direction, "scene.camera");
  camera.viewProjection = OptionalFloatArray<16>(
      *cameraValue, "viewProjection", camera.viewProjection, "scene.camera");
  camera.rowMajor = OptionalJsonBool(*cameraValue, "rowMajor", camera.rowMajor,
                                     "scene.camera");
  return camera;
}

std::vector<MeshAsset> ParseMeshes(const JsonValue& root) {
  const JsonValue& assetsValue = RequireMember(root, "assets", "scene");
  const JsonValue& modelsValue =
      RequireMember(assetsValue, kModelsCategory, "scene.assets");
  RequireType(modelsValue, JsonValue::Type::kArray, "scene.assets.models");
  if (modelsValue.array.empty()) {
    throw std::runtime_error("scene.assets.models must not be empty");
  }

  std::vector<MeshAsset> meshes;
  meshes.reserve(modelsValue.array.size());
  for (std::size_t i = 0; i < modelsValue.array.size(); ++i) {
    const std::string context =
        "scene.assets.models[" + std::to_string(i) + "]";
    const JsonValue& meshValue = modelsValue.array[i];
    RequireType(meshValue, JsonValue::Type::kObject, context);

    MeshAsset mesh;
    mesh.id =
        JsonString(RequireMember(meshValue, "id", context), context + ".id");
    mesh.file = JsonString(RequireMember(meshValue, "file", context),
                           context + ".file");
    mesh.normalize =
        OptionalJsonBool(meshValue, "normalize", mesh.normalize, context);
    ValidateCategorizedAssetPath(mesh.file, context + ".file");
    meshes.push_back(std::move(mesh));
  }
  return meshes;
}

std::array<float, 16> ParseInstanceTransform(const JsonValue& instanceValue,
                                             std::string_view context) {
  if (const JsonValue* matrixValue = FindMember(instanceValue, "matrix")) {
    return JsonFloatArray<16>(*matrixValue, std::string(context) + ".matrix");
  }

  if (const JsonValue* transformValue =
          FindMember(instanceValue, "transform")) {
    RequireType(*transformValue, JsonValue::Type::kObject,
                std::string(context) + ".transform");
    const std::array<float, 3> defaultTranslation{0.0f, 0.0f, 0.0f};
    const std::array<float, 3> translation = OptionalFloatArray<3>(
        *transformValue, "translation", defaultTranslation,
        std::string(context) + ".transform");
    const float scale = OptionalJsonFloat(*transformValue, "scale", 1.0f,
                                          std::string(context) + ".transform");
    const float yawRadians =
        OptionalJsonFloat(*transformValue, "yawRadians", 0.0f,
                          std::string(context) + ".transform");
    const float rollRadians =
        OptionalJsonFloat(*transformValue, "rollRadians", 0.0f,
                          std::string(context) + ".transform");
    return MakeColumnMajorTransform(translation[0], translation[1],
                                    translation[2], scale, yawRadians,
                                    rollRadians);
  }

  return Instance{}.localToWorld;
}

std::vector<Instance> ParseInstances(const JsonValue& root) {
  const JsonValue& instancesValue = RequireMember(root, "instances", "scene");
  RequireType(instancesValue, JsonValue::Type::kArray, "scene.instances");
  if (instancesValue.array.empty()) {
    throw std::runtime_error("scene.instances must not be empty");
  }

  std::vector<Instance> instances;
  instances.reserve(instancesValue.array.size());
  for (std::size_t i = 0; i < instancesValue.array.size(); ++i) {
    const std::string context = "scene.instances[" + std::to_string(i) + "]";
    const JsonValue& instanceValue = instancesValue.array[i];
    RequireType(instanceValue, JsonValue::Type::kObject, context);

    Instance instance;
    instance.name = OptionalJsonString(instanceValue, "name", "", context);
    instance.meshId = JsonString(RequireMember(instanceValue, "mesh", context),
                                 context + ".mesh");
    instance.localToWorld = ParseInstanceTransform(instanceValue, context);
    instance.rowMajor =
        OptionalJsonBool(instanceValue, "rowMajor", instance.rowMajor, context);
    instance.backfaceCull = OptionalJsonBool(instanceValue, "backfaceCull",
                                             instance.backfaceCull, context);
    instances.push_back(std::move(instance));
  }
  return instances;
}

std::string JsonEscape(std::string_view value) {
  std::string escaped;
  for (char c : value) {
    switch (c) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20u) {
          escaped += "\\u00";
          constexpr char hex[] = "0123456789abcdef";
          escaped.push_back(hex[(static_cast<unsigned char>(c) >> 4) & 0x0F]);
          escaped.push_back(hex[static_cast<unsigned char>(c) & 0x0F]);
        } else {
          escaped.push_back(c);
        }
        break;
    }
  }
  return escaped;
}

std::string MakePathRelativeTo(const std::filesystem::path& path,
                               const std::filesystem::path& baseDir) {
  std::error_code error;
  const std::filesystem::path absolutePath =
      std::filesystem::absolute(path, error).lexically_normal();
  if (error) {
    return path.generic_string();
  }

  const std::filesystem::path absoluteBase =
      std::filesystem::absolute(baseDir, error).lexically_normal();
  if (error) {
    return absolutePath.generic_string();
  }

  const std::filesystem::path relative =
      absolutePath.lexically_relative(absoluteBase);
  if (!relative.empty()) {
    return relative.generic_string();
  }
  return absolutePath.generic_string();
}

int ParseObjIndex(const std::string& token, int vertexCount) {
  const std::size_t slash = token.find('/');
  const std::string indexText = token.substr(0, slash);
  if (indexText.empty()) {
    throw std::runtime_error("OBJ face has an empty vertex index");
  }

  int rawIndex = 0;
  const char* first = indexText.data();
  const char* last = first + indexText.size();
  const auto result = std::from_chars(first, last, rawIndex);
  if (result.ec != std::errc() || result.ptr != last || rawIndex == 0) {
    throw std::runtime_error("OBJ face has an invalid vertex index: " + token);
  }

  const int zeroBased = rawIndex > 0 ? rawIndex - 1 : vertexCount + rawIndex;
  if (zeroBased < 0 || zeroBased >= vertexCount) {
    throw std::runtime_error(
        "OBJ face references a vertex outside the loaded range");
  }
  return zeroBased;
}

void NormalizeMesh(Mesh& mesh) {
  if (mesh.vertices.empty()) {
    return;
  }

  Vec3 minValue{std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity()};
  Vec3 maxValue{-std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity()};

  for (std::size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
    minValue.x = std::min(minValue.x, mesh.vertices[i + 0]);
    minValue.y = std::min(minValue.y, mesh.vertices[i + 1]);
    minValue.z = std::min(minValue.z, mesh.vertices[i + 2]);
    maxValue.x = std::max(maxValue.x, mesh.vertices[i + 0]);
    maxValue.y = std::max(maxValue.y, mesh.vertices[i + 1]);
    maxValue.z = std::max(maxValue.z, mesh.vertices[i + 2]);
  }

  const Vec3 center{(minValue.x + maxValue.x) * 0.5f,
                    (minValue.y + maxValue.y) * 0.5f,
                    (minValue.z + maxValue.z) * 0.5f};
  const float extentX = maxValue.x - minValue.x;
  const float extentY = maxValue.y - minValue.y;
  const float extentZ = maxValue.z - minValue.z;
  const float maxExtent = std::max({extentX, extentY, extentZ});
  if (maxExtent <= 0.0f) {
    throw std::runtime_error(mesh.name + " has zero-sized bounds");
  }

  const float invExtent = 1.0f / maxExtent;
  for (std::size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
    mesh.vertices[i + 0] = (mesh.vertices[i + 0] - center.x) * invExtent;
    mesh.vertices[i + 1] = (mesh.vertices[i + 1] - center.y) * invExtent;
    mesh.vertices[i + 2] = (mesh.vertices[i + 2] - center.z) * invExtent;
  }
}

Mesh LoadObj(const std::filesystem::path& path, bool normalize) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Failed to open OBJ: " + path.string());
  }

  Mesh mesh;
  mesh.name = path.filename().string();

  std::string line;
  std::size_t lineNumber = 0;
  while (std::getline(input, line)) {
    ++lineNumber;
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }

    std::istringstream stream(trimmed);
    std::string tag;
    stream >> tag;

    if (tag == "v") {
      Vec3 vertex;
      if (!(stream >> vertex.x >> vertex.y >> vertex.z)) {
        throw std::runtime_error("Invalid vertex line in " + mesh.name + ":" +
                                 std::to_string(lineNumber));
      }
      mesh.vertices.push_back(vertex.x);
      mesh.vertices.push_back(vertex.y);
      mesh.vertices.push_back(vertex.z);
      if ((mesh.vertices.size() / 3) >
          std::numeric_limits<unsigned short>::max()) {
        throw std::runtime_error(mesh.name +
                                 " has more than 65535 vertices, which exceeds "
                                 "the C API index format");
      }
    } else if (tag == "f") {
      std::vector<unsigned short> face;
      std::string token;
      const int vertexCount = static_cast<int>(mesh.vertices.size() / 3);
      while (stream >> token) {
        const int index = ParseObjIndex(token, vertexCount);
        face.push_back(static_cast<unsigned short>(index));
      }

      if (face.size() < 3) {
        throw std::runtime_error("Invalid face line in " + mesh.name + ":" +
                                 std::to_string(lineNumber));
      }
      for (std::size_t i = 1; i + 1 < face.size(); ++i) {
        mesh.indices.push_back(face[0]);
        mesh.indices.push_back(face[i]);
        mesh.indices.push_back(face[i + 1]);
      }
    }
  }

  if (mesh.vertices.empty() || mesh.indices.empty()) {
    throw std::runtime_error("OBJ did not contain renderable geometry: " +
                             path.string());
  }

  if (normalize) {
    NormalizeMesh(mesh);
  }
  return mesh;
}
}  // namespace

std::filesystem::path DefaultAssetsDir() {
  return std::filesystem::path("tests") / "render" / "assets";
}

std::filesystem::path DefaultScenePath(const std::filesystem::path& assetsDir) {
  return assetsDir / kScenesCategory / "depth_scene.json";
}

std::filesystem::path DefaultOutputPath() {
  return std::filesystem::path("artifacts") / "depth_scene_demo" / "depth.png";
}

Scene CreateDefaultDepthScene(const std::filesystem::path& assetsDir) {
  Scene scene;
  scene.assetRoot = NormalizeRootPath(assetsDir);

  for (const auto& [id, file] : DefaultMeshSpecs()) {
    MeshAsset mesh;
    mesh.id = id;
    mesh.file = file;
    mesh.normalize = true;
    scene.meshes.push_back(std::move(mesh));
  }

  for (const DefaultInstanceSpec& spec : DefaultInstanceSpecs()) {
    Instance instance;
    instance.name = spec.name;
    instance.meshId = spec.meshId;
    instance.localToWorld =
        MakeColumnMajorTransform(spec.tx, spec.ty, spec.tz, spec.scale,
                                 spec.yawRadians, spec.rollRadians);
    scene.instances.push_back(std::move(instance));
  }

  return scene;
}

Scene LoadSceneFromJson(const std::filesystem::path& scenePath,
                        const std::filesystem::path& fallbackAssetsDir) {
  const std::string source = ReadTextFile(scenePath);
  JsonParser parser(source);
  const JsonValue root = parser.Parse();
  RequireType(root, JsonValue::Type::kObject, "scene");

  if (const JsonValue* version = FindMember(root, "version")) {
    const unsigned int sceneVersion = JsonUnsigned(*version, "scene.version");
    if (sceneVersion != 1) {
      throw std::runtime_error("Unsupported scene.version: " +
                               std::to_string(sceneVersion));
    }
  }

  const std::filesystem::path baseDir = SceneBaseDir(scenePath);
  Scene scene;
  scene.render = ParseRenderSettings(root);
  scene.camera = ParseCamera(root);
  scene.meshes = ParseMeshes(root);
  scene.instances = ParseInstances(root);

  const std::string assetRoot =
      OptionalJsonString(root, "assetRoot", "", "scene");
  const std::filesystem::path resolvedAssetRoot =
      assetRoot.empty() ? fallbackAssetsDir
                        : ResolveRelativePath(baseDir, assetRoot);
  scene.assetRoot = NormalizeRootPath(resolvedAssetRoot);
  const std::filesystem::path expectedAssetRoot =
      NormalizeRootPath(fallbackAssetsDir);
  if (scene.assetRoot != expectedAssetRoot) {
    throw std::runtime_error(
        "scene.assetRoot must resolve to the configured assets directory: " +
        expectedAssetRoot.string());
  }

  if (const JsonValue* outputValue = FindMember(root, "output")) {
    RequireType(*outputValue, JsonValue::Type::kObject, "scene.output");
    const std::string output =
        OptionalJsonString(*outputValue, "path", "", "scene.output");
    if (!output.empty()) {
      scene.outputPath = ResolveRelativePath(baseDir, output);
    }
  }

  return scene;
}

void WriteDefaultDepthSceneJson(const std::filesystem::path& scenePath,
                                const std::filesystem::path& assetsDir,
                                const std::filesystem::path& outputPath) {
  const std::filesystem::path parent = SceneBaseDir(scenePath);
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }

  std::ofstream output(scenePath, std::ios::binary);
  if (!output) {
    throw std::runtime_error("Failed to open scene JSON for writing: " +
                             scenePath.string());
  }

  output << "{\n";
  output << "  \"version\": 1,\n";
  output << "  \"assetRoot\": \""
         << JsonEscape(MakePathRelativeTo(assetsDir, parent)) << "\",\n";
  output << "  \"render\": {\n";
  output << "    \"width\": 1024,\n";
  output << "    \"height\": 1024,\n";
  output << "    \"nearPlane\": 0.01,\n";
  output << "    \"mode\": \"full\",\n";
  output << "    \"counterClockwise\": true,\n";
  output << "    \"visualizeDepth\": true\n";
  output << "  },\n";
  output << "  \"output\": {\n";
  output << "    \"path\": \""
         << JsonEscape(MakePathRelativeTo(outputPath, parent)) << "\"\n";
  output << "  },\n";
  output << "  \"camera\": {\n";
  output << "    \"position\": [0, 0, 0],\n";
  output << "    \"direction\": [0, 0, 1],\n";
  output << "    \"viewProjection\": [\n";
  output << "      1, 0, 0, 0,\n";
  output << "      0, 1, 0, 0,\n";
  output << "      0, 0, 1, 0,\n";
  output << "      0, 0, 0, 1\n";
  output << "    ],\n";
  output << "    \"rowMajor\": false\n";
  output << "  },\n";
  output << "  \"assets\": {\n";
  output << "    \"models\": [\n";
  const auto& meshes = DefaultMeshSpecs();
  for (std::size_t i = 0; i < meshes.size(); ++i) {
    output << "      { \"id\": \"" << meshes[i].first << "\", \"file\": \""
           << meshes[i].second << "\", \"normalize\": true }"
           << (i + 1 == meshes.size() ? "\n" : ",\n");
  }
  output << "    ]\n";
  output << "  },\n";
  output << "  \"instances\": [\n";
  const auto& instances = DefaultInstanceSpecs();
  for (std::size_t i = 0; i < instances.size(); ++i) {
    const DefaultInstanceSpec& instance = instances[i];
    output << "    {\n";
    output << "      \"name\": \"" << JsonEscape(instance.name) << "\",\n";
    output << "      \"mesh\": \"" << JsonEscape(instance.meshId) << "\",\n";
    output << "      \"transform\": {\n";
    output << "        \"translation\": [" << instance.tx << ", " << instance.ty
           << ", " << instance.tz << "],\n";
    output << "        \"scale\": " << instance.scale << ",\n";
    output << "        \"yawRadians\": " << instance.yawRadians << ",\n";
    output << "        \"rollRadians\": " << instance.rollRadians << "\n";
    output << "      },\n";
    output << "      \"backfaceCull\": false\n";
    output << "    }" << (i + 1 == instances.size() ? "\n" : ",\n");
  }
  output << "  ]\n";
  output << "}\n";
}

void LoadSceneMeshes(Scene& scene, std::vector<Mesh>& meshes) {
  meshes.clear();
  meshes.reserve(scene.meshes.size());

  std::unordered_map<std::string, const Mesh*> meshById;
  for (const MeshAsset& asset : scene.meshes) {
    if (meshById.find(asset.id) != meshById.end()) {
      throw std::runtime_error("Duplicate model asset id in scene: " +
                               asset.id);
    }

    const std::filesystem::path meshPath =
        (scene.assetRoot / kModelsCategory / asset.file).lexically_normal();
    meshes.push_back(LoadObj(meshPath, asset.normalize));
    meshById.emplace(asset.id, &meshes.back());
  }

  for (Instance& instance : scene.instances) {
    const auto it = meshById.find(instance.meshId);
    if (it == meshById.end()) {
      throw std::runtime_error("Instance references unknown model asset id: " +
                               instance.meshId);
    }
    instance.mesh = it->second;
  }
}

void ValidateRenderSettings(const RenderSettings& render) {
  if (render.width < 64 || (render.width % 64u) != 0u) {
    throw std::runtime_error(
        "Render width must be at least 64 and divisible by 64");
  }
  if (render.height < 8 || (render.height % 8u) != 0u) {
    throw std::runtime_error(
        "Render height must be at least 8 and divisible by 8");
  }
  if (render.nearPlane < CULLING_ENGINE_MIN_NEAR_PLANE) {
    throw std::runtime_error(
        "Render nearPlane must be at least CULLING_ENGINE_MIN_NEAR_PLANE");
  }
}
}  // namespace CullingEngineExample
