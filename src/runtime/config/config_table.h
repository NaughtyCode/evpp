#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime/core/engine_api.h"

namespace engine {

// ConfigTable — typed in-memory data table loaded from JSON or CSV.
//
// Rows are key-value maps. Indexes enable O(1) row lookup by column value.
// JSON format: array of objects (one object per row).
// CSV format: header row + data rows.

class ENGINE_API ConfigTable {
public:
    enum class ColumnType { Int, Float, Bool, String };

    struct ColumnInfo {
        std::string name;
        ColumnType type = ColumnType::String;
    };

    using RowMap = std::unordered_map<std::string, std::string>;

    ConfigTable() = default;

    bool LoadFromJson(const std::string& path);
    bool LoadFromCsv(const std::string& path);

    // Typed value access: find row where key_column == key_value,
    // return value of value_column as T.
    int         GetInt(const std::string& key_column, const std::string& key_value,
                       const std::string& value_column, int default_val = 0) const;
    double      GetFloat(const std::string& key_column, const std::string& key_value,
                         const std::string& value_column, double default_val = 0.0) const;
    bool        GetBool(const std::string& key_column, const std::string& key_value,
                        const std::string& value_column, bool default_val = false) const;
    std::string GetString(const std::string& key_column, const std::string& key_value,
                          const std::string& value_column,
                          const std::string& default_val = "") const;

    // Full row lookup by key column value. Returns nullptr if not found.
    const RowMap* GetRow(const std::string& key_column,
                         const std::string& key_value) const;

    // Row by position (0-based).
    const RowMap* GetRowByIndex(size_t index) const;

    // Build a hash index on a column for O(1) key lookup.
    void BuildIndex(const std::string& column);
    bool HasIndex(const std::string& column) const;

    // Accessors.
    size_t RowCount() const { return rows_.size(); }
    const std::vector<ColumnInfo>& Columns() const { return columns_; }
    const std::vector<RowMap>& Rows() const { return rows_; }
    bool IsLoaded() const { return !rows_.empty(); }

private:
    ColumnType InferType(const std::string& value) const;
    void InferColumnTypes();
    std::string Trim(const std::string& s) const;
    size_t FindRowIndex(const std::string& key_column,
                        const std::string& key_value) const;

    std::vector<ColumnInfo> columns_;
    std::vector<RowMap> rows_;
    // column -> (key_value -> row_index)
    std::unordered_map<std::string, std::unordered_map<std::string, size_t>> indexes_;
};

}  // namespace engine
