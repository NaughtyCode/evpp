#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "runtime/config/config_table.h"
#include "runtime/core/engine_api.h"

namespace engine {

// ReferenceValidator — validates cross-table foreign-key references.
//
// After registering tables and references, Validate() checks that every
// referenced value exists in the target table's ID column. Also supports
// topological sort for load-order detection and cycle detection.
//
// Usage:
//   validator.AddTable("monster", monster_table, "id");
//   validator.AddTable("item", item_table, "id");
//   validator.AddReference("monster", "drop_table_id", "drop_table");
//   validator.AddReference("drop_table.items", "item_id", "item");
//   auto result = validator.Validate();
//   if (!result.valid) { ... log result.errors ... }

class ENGINE_API ReferenceValidator {
public:
    struct RefError {
        std::string from_table;
        std::string from_column;
        std::string from_row_id;   // row identifier in source table
        std::string to_table;
        std::string missing_value; // referenced value not found
    };

    struct Result {
        bool valid = true;
        std::vector<RefError> errors;
        std::vector<std::string> load_order;  // topologically sorted
        bool has_cycle = false;
    };

    // Register a table with its primary key column.
    void AddTable(const std::string& name, const ConfigTable* table,
                  const std::string& id_column);

    // Define a reference: from_table.from_column → to_table (on to_table's id column).
    void AddReference(const std::string& from_table, const std::string& from_column,
                      const std::string& to_table);

    // Validate all registered references. Returns Result with any broken refs.
    Result Validate() const;

    // Compute topological load order (tables with no deps first).
    // Returns empty vector if there's a cycle.
    std::vector<std::string> TopologicalSort() const;

private:
    struct TableEntry {
        const ConfigTable* table = nullptr;
        std::string id_column;
    };

    struct RefEntry {
        std::string from_table;
        std::string from_column;
        std::string to_table;
    };

    std::unordered_map<std::string, TableEntry> tables_;
    std::vector<RefEntry> references_;
};

}  // namespace engine
