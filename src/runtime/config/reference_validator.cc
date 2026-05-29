#include "runtime/config/reference_validator.h"

#include <algorithm>
#include <queue>

#include "runtime/core/log/log.h"

namespace engine {

void ReferenceValidator::AddTable(const std::string& name, const ConfigTable* table,
                                   const std::string& id_column) {
    tables_[name] = {table, id_column};
}

void ReferenceValidator::AddReference(const std::string& from_table,
                                       const std::string& from_column,
                                       const std::string& to_table) {
    references_.push_back({from_table, from_column, to_table});
}

ReferenceValidator::Result ReferenceValidator::Validate() const {
    Result result;

    for (const auto& ref : references_) {
        // Check that both tables are registered.
        auto from_it = tables_.find(ref.from_table);
        if (from_it == tables_.end()) {
            result.valid = false;
            result.errors.push_back(
                {ref.from_table, ref.from_column, "",
                 ref.to_table, "",
                 });
            if (auto* l = GetLogger())
                ENGINE_LOG_ERROR(l, "ReferenceValidator: from_table '{}' not registered",
                                 ref.from_table);
            continue;
        }

        auto to_it = tables_.find(ref.to_table);
        if (to_it == tables_.end()) {
            result.valid = false;
            result.errors.push_back(
                {ref.from_table, ref.from_column, "",
                 ref.to_table, ""});
            if (auto* l = GetLogger())
                ENGINE_LOG_ERROR(l, "ReferenceValidator: to_table '{}' not registered",
                                 ref.to_table);
            continue;
        }

        const ConfigTable* from_tbl = from_it->second.table;
        const ConfigTable* to_tbl = to_it->second.table;
        const std::string& to_id_col = to_it->second.id_column;

        if (!from_tbl || !to_tbl) continue;

        // For each row in the source table, check the referenced value exists
        // in the target table's ID column.
        const auto& from_cols = from_tbl->Columns();
        const auto& from_rows = from_tbl->Rows();

        // Find the column index for the from_column.
        bool has_col = false;
        for (const auto& col : from_cols) {
            if (col.name == ref.from_column) {
                has_col = true;
                break;
            }
        }
        if (!has_col) {
            // Column not found — skip (may be an optional reference)
            continue;
        }

        for (size_t i = 0; i < from_rows.size(); ++i) {
            const auto& row = from_rows[i];
            auto val_it = row.find(ref.from_column);
            if (val_it == row.end() || val_it->second.empty()) continue;

            const std::string& ref_value = val_it->second;

            // Check if this value exists in the target table.
            const auto* target_row = to_tbl->GetRow(to_id_col, ref_value);
            if (!target_row) {
                result.valid = false;

                // Get source row ID for error reporting.
                std::string source_id = std::to_string(i + 1);
                auto src_id_it = row.find(from_it->second.id_column);
                if (src_id_it != row.end()) {
                    source_id = src_id_it->second;
                }

                result.errors.push_back(
                    {ref.from_table, ref.from_column, source_id,
                     ref.to_table, ref_value});
            }
        }
    }

    // Topological sort for load order.
    result.load_order = TopologicalSort();
    result.has_cycle = result.load_order.empty() && !tables_.empty() && !references_.empty();

    if (result.has_cycle) {
        result.valid = false;
        if (auto* l = GetLogger())
            ENGINE_LOG_ERROR(l, "ReferenceValidator: circular dependency detected");
    }

    return result;
}

std::vector<std::string> ReferenceValidator::TopologicalSort() const {
    // Build dependency graph.
    // Each reference: from_table depends on to_table (to_table must load first).
    std::unordered_map<std::string, int> in_degree;
    std::unordered_map<std::string, std::vector<std::string>> adjacency;

    for (const auto& [name, entry] : tables_) {
        in_degree[name] = 0;  // ensure all tables appear in in_degree
    }

    for (const auto& ref : references_) {
        // from depends on to → edge to → from
        adjacency[ref.to_table].push_back(ref.from_table);
        // Ensure both nodes exist in in_degree
        if (in_degree.find(ref.to_table) == in_degree.end()) in_degree[ref.to_table] = 0;
        if (in_degree.find(ref.from_table) == in_degree.end()) in_degree[ref.from_table] = 0;
        in_degree[ref.from_table]++;
    }

    // Kahn's algorithm.
    std::queue<std::string> q;
    for (const auto& [name, deg] : in_degree) {
        if (deg == 0) q.push(name);
    }

    std::vector<std::string> sorted;
    while (!q.empty()) {
        std::string node = q.front();
        q.pop();
        sorted.push_back(node);

        for (const auto& neighbor : adjacency[node]) {
            if (--in_degree[neighbor] == 0) {
                q.push(neighbor);
            }
        }
    }

    // If sorted.size() != tables_.size(), there's a cycle.
    // For tables with no references, they should still appear.
    if (sorted.size() != tables_.size()) {
        // Add any remaining tables that weren't in the graph.
        for (const auto& [name, entry] : tables_) {
            if (std::find(sorted.begin(), sorted.end(), name) == sorted.end()) {
                sorted.push_back(name);
            }
        }
    }

    return sorted;
}

}  // namespace engine
