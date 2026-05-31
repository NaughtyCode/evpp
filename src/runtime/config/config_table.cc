#include "runtime/config/config_table.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

#include "runtime/config/path_resolver.h"
#include "runtime/core/log/log.h"

namespace engine {

namespace {

bool IsInteger(const std::string& s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[0] == '-') i = 1;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

bool IsFloat(const std::string& s) {
    if (s.empty()) return false;
    char* end = nullptr;
    std::strtod(s.c_str(), &end);
    return end == s.c_str() + s.size() && end != s.c_str();
}

bool IsBool(const std::string& s) {
    return s == "true" || s == "false" || s == "TRUE" || s == "FALSE";
}

bool ParseIntStrict(const std::string& s, int& out) {
    std::string v = s;
    char* end = nullptr;
    errno = 0;
    long value = std::strtol(v.c_str(), &end, 10);
    if (errno != 0 || end == v.c_str() || *end != '\0' ||
        value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

bool ParseDoubleStrict(const std::string& s, double& out) {
    std::string v = s;
    char* end = nullptr;
    errno = 0;
    double value = std::strtod(v.c_str(), &end);
    if (errno != 0 || end == v.c_str() || *end != '\0') {
        return false;
    }
    out = value;
    return true;
}

bool ReadCompositeJsonValue(const std::string& json, size_t& pos, std::string& value) {
    const size_t start = pos;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;

    while (pos < json.size()) {
        const char c = json[pos++];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (in_string) {
            if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        if (c == '"') {
            in_string = true;
            continue;
        }
        if (c == '{' || c == '[') {
            ++depth;
            continue;
        }
        if (c == '}' || c == ']') {
            --depth;
            if (depth == 0) {
                value = json.substr(start, pos - start);
                return true;
            }
        }
    }

    return false;
}

}  // namespace

std::string ConfigTable::Trim(const std::string& s) const {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

ConfigTable::ColumnType ConfigTable::InferType(const std::string& value) const {
    std::string v = Trim(value);
    if (v.empty()) return ColumnType::String;
    if (IsInteger(v)) return ColumnType::Int;
    if (IsFloat(v)) return ColumnType::Float;
    if (IsBool(v)) return ColumnType::Bool;
    return ColumnType::String;
}

void ConfigTable::InferColumnTypes() {
    if (columns_.empty() || rows_.empty()) return;

    for (auto& col : columns_) {
        // Start with the tightest type and widen as needed.
        ColumnType inferred = ColumnType::Int;
        for (const auto& row : rows_) {
            auto it = row.find(col.name);
            if (it == row.end()) continue;
            ColumnType t = InferType(it->second);
            if (t == ColumnType::String) {
                inferred = ColumnType::String;
                break;
            }
            if (t == ColumnType::Float && inferred == ColumnType::Int) {
                inferred = ColumnType::Float;
            }
            if (t == ColumnType::Bool && inferred != ColumnType::Bool && inferred != ColumnType::String) {
                // bool values can coexist with other types as string
            }
        }
        col.type = inferred;
    }
}

// ── JSON loading ────────────────────────────────────────────────────────

bool ConfigTable::LoadFromJson(const std::string& path) {
    const auto resolved_path = config::ResolvePathFromWorkingTree(path);
    std::ifstream ifs(resolved_path);
    if (!ifs.is_open()) {
        if (auto* l = GetLogger())
            ENGINE_LOG_ERROR(l, "ConfigTable: cannot open JSON file [{}]", path);
        return false;
    }

    std::stringstream buf;
    buf << ifs.rdbuf();
    std::string json = buf.str();

    // Simple streaming JSON parser for array-of-objects.
    size_t pos = 0;
    // Skip whitespace to opening '['
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
           json[pos] == '\n' || json[pos] == '\r')) ++pos;
    if (pos >= json.size() || json[pos] != '[') {
        if (auto* l = GetLogger())
            ENGINE_LOG_ERROR(l, "ConfigTable: [{}] is not a JSON array", path);
        return false;
    }
    ++pos;  // skip '['

    rows_.clear();
    columns_.clear();
    indexes_.clear();
    std::unordered_map<std::string, size_t> col_index;

    while (pos < json.size()) {
        // Skip whitespace
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
               json[pos] == '\n' || json[pos] == '\r')) ++pos;
        if (pos >= json.size()) break;
        if (json[pos] == ']') break;
        if (json[pos] == ',') { ++pos; continue; }
        if (json[pos] != '{') {
            if (auto* l = GetLogger())
                ENGINE_LOG_ERROR(l, "ConfigTable: [{}] expected object at pos {}", path, pos);
            return false;
        }
        ++pos;  // skip '{'

        RowMap row;
        while (pos < json.size()) {
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                   json[pos] == '\n' || json[pos] == '\r')) ++pos;
            if (pos >= json.size()) break;
            if (json[pos] == '}') break;

            // Read key (string)
            if (json[pos] != '"') {
                if (auto* l = GetLogger())
                    ENGINE_LOG_ERROR(l, "ConfigTable: [{}] expected string key at pos {}", path, pos);
                return false;
            }
            ++pos;  // skip opening "
            std::string key;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\') {
                    ++pos;
                    if (pos < json.size()) {
                        switch (json[pos]) {
                        case 'n': key += '\n'; break;
                        case 't': key += '\t'; break;
                        case '"': key += '"'; break;
                        case '\\': key += '\\'; break;
                        default: key += json[pos]; break;
                        }
                    }
                } else {
                    key += json[pos];
                }
                ++pos;
            }
            if (pos < json.size()) ++pos;  // skip closing "

            // Skip ':'
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                   json[pos] == '\n' || json[pos] == '\r')) ++pos;
            if (pos < json.size() && json[pos] == ':') ++pos;

            // Read value (string, number, bool, null)
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                   json[pos] == '\n' || json[pos] == '\r')) ++pos;
            if (pos >= json.size()) break;

            std::string value;
            if (json[pos] == '"') {
                ++pos;  // skip opening "
                while (pos < json.size() && json[pos] != '"') {
                    if (json[pos] == '\\') {
                        ++pos;
                        if (pos < json.size()) {
                            switch (json[pos]) {
                            case 'n': value += '\n'; break;
                            case 't': value += '\t'; break;
                            case '"': value += '"'; break;
                            case '\\': value += '\\'; break;
                            default: value += json[pos]; break;
                            }
                        }
                    } else {
                        value += json[pos];
                    }
                    ++pos;
                }
                if (pos < json.size()) ++pos;  // skip closing "
            } else if (json[pos] == '-' || (json[pos] >= '0' && json[pos] <= '9')) {
                size_t start = pos;
                if (json[pos] == '-') ++pos;
                while (pos < json.size() && (json[pos] >= '0' && json[pos] <= '9')) ++pos;
                if (pos < json.size() && json[pos] == '.') {
                    ++pos;
                    while (pos < json.size() && (json[pos] >= '0' && json[pos] <= '9')) ++pos;
                }
                value = json.substr(start, pos - start);
            } else if (json.compare(pos, 4, "true") == 0) {
                value = "true"; pos += 4;
            } else if (json.compare(pos, 5, "false") == 0) {
                value = "false"; pos += 5;
            } else if (json.compare(pos, 4, "null") == 0) {
                value = ""; pos += 4;
            } else if (json[pos] == '{' || json[pos] == '[') {
                if (!ReadCompositeJsonValue(json, pos, value)) {
                    if (auto* l = GetLogger())
                        ENGINE_LOG_ERROR(l, "ConfigTable: [{}] unterminated nested JSON value at pos {}",
                                         path, pos);
                    return false;
                }
            } else {
                if (auto* l = GetLogger())
                    ENGINE_LOG_ERROR(l, "ConfigTable: [{}] unexpected char '{}' at pos {}",
                                     path, json[pos], pos);
                return false;
            }

            row[key] = value;

            // Track column order
            if (col_index.find(key) == col_index.end()) {
                col_index[key] = columns_.size();
                columns_.push_back({key, ColumnType::String});
            }

            // Skip ','
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                   json[pos] == '\n' || json[pos] == '\r')) ++pos;
            if (pos < json.size() && json[pos] == ',') ++pos;
        }
        if (pos < json.size() && json[pos] == '}') ++pos;  // skip '}'

        rows_.push_back(std::move(row));
    }

    InferColumnTypes();

    if (auto* l = GetLogger())
        ENGINE_LOG_INFO(l, "ConfigTable: loaded [{}] — {} rows, {} columns",
                        path, rows_.size(), columns_.size());
    return true;
}

// ── CSV loading ─────────────────────────────────────────────────────────

bool ConfigTable::LoadFromCsv(const std::string& path) {
    const auto resolved_path = config::ResolvePathFromWorkingTree(path);
    std::ifstream ifs(resolved_path);
    if (!ifs.is_open()) {
        if (auto* l = GetLogger())
            ENGINE_LOG_ERROR(l, "ConfigTable: cannot open CSV file [{}]", path);
        return false;
    }

    std::string line;

    // Read header row.
    if (!std::getline(ifs, line)) {
        return false;
    }

    columns_.clear();
    indexes_.clear();
    std::stringstream header_ss(line);
    std::string col_name;
    while (std::getline(header_ss, col_name, ',')) {
        col_name = Trim(col_name);
        columns_.push_back({col_name, ColumnType::String});
    }

    // Read data rows.
    rows_.clear();
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;

        RowMap row;
        std::stringstream row_ss(line);
        std::string value;
        size_t col_idx = 0;
        while (std::getline(row_ss, value, ',')) {
            value = Trim(value);
            if (col_idx < columns_.size()) {
                row[columns_[col_idx].name] = value;
            }
            ++col_idx;
        }
        rows_.push_back(std::move(row));
    }

    InferColumnTypes();

    if (auto* l = GetLogger())
        ENGINE_LOG_INFO(l, "ConfigTable: loaded [{}] — {} rows, {} columns",
                        path, rows_.size(), columns_.size());
    return true;
}

// ── Indexing ────────────────────────────────────────────────────────────

void ConfigTable::BuildIndex(const std::string& column) {
    auto& idx = indexes_[column];
    idx.clear();
    for (size_t i = 0; i < rows_.size(); ++i) {
        auto it = rows_[i].find(column);
        if (it != rows_[i].end()) {
            idx[it->second] = i;
        }
    }
}

bool ConfigTable::HasIndex(const std::string& column) const {
    return indexes_.find(column) != indexes_.end();
}

size_t ConfigTable::FindRowIndex(const std::string& key_column,
                                  const std::string& key_value) const {
    // Try index first.
    auto idx_it = indexes_.find(key_column);
    if (idx_it != indexes_.end()) {
        auto row_it = idx_it->second.find(key_value);
        if (row_it != idx_it->second.end()) {
            return row_it->second;
        }
        return static_cast<size_t>(-1);
    }

    // Linear scan.
    for (size_t i = 0; i < rows_.size(); ++i) {
        auto it = rows_[i].find(key_column);
        if (it != rows_[i].end() && it->second == key_value) {
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

// ── Accessors ───────────────────────────────────────────────────────────

const ConfigTable::RowMap* ConfigTable::GetRow(const std::string& key_column,
                                                const std::string& key_value) const {
    size_t idx = FindRowIndex(key_column, key_value);
    if (idx == static_cast<size_t>(-1)) return nullptr;
    return &rows_[idx];
}

const ConfigTable::RowMap* ConfigTable::GetRowByIndex(size_t index) const {
    if (index >= rows_.size()) return nullptr;
    return &rows_[index];
}

int ConfigTable::GetInt(const std::string& key_column, const std::string& key_value,
                         const std::string& value_column, int default_val) const {
    const RowMap* row = GetRow(key_column, key_value);
    if (!row) return default_val;
    auto it = row->find(value_column);
    if (it == row->end() || it->second.empty()) return default_val;
    int value = default_val;
    return ParseIntStrict(Trim(it->second), value) ? value : default_val;
}

double ConfigTable::GetFloat(const std::string& key_column, const std::string& key_value,
                              const std::string& value_column, double default_val) const {
    const RowMap* row = GetRow(key_column, key_value);
    if (!row) return default_val;
    auto it = row->find(value_column);
    if (it == row->end() || it->second.empty()) return default_val;
    double value = default_val;
    return ParseDoubleStrict(Trim(it->second), value) ? value : default_val;
}

bool ConfigTable::GetBool(const std::string& key_column, const std::string& key_value,
                           const std::string& value_column, bool default_val) const {
    const RowMap* row = GetRow(key_column, key_value);
    if (!row) return default_val;
    auto it = row->find(value_column);
    if (it == row->end()) return default_val;
    std::string v = Trim(it->second);
    if (v == "true" || v == "TRUE" || v == "1") return true;
    if (v == "false" || v == "FALSE" || v == "0") return false;
    return default_val;
}

std::string ConfigTable::GetString(const std::string& key_column,
                                    const std::string& key_value,
                                    const std::string& value_column,
                                    const std::string& default_val) const {
    const RowMap* row = GetRow(key_column, key_value);
    if (!row) return default_val;
    auto it = row->find(value_column);
    if (it == row->end()) return default_val;
    return it->second;
}

}  // namespace engine
