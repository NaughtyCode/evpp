--- Module Import API
--- Global function: import. The function object itself also carries sub-functions as fields.
---
--- Module names use dot-separated paths that automatically map to the filesystem:
---   import("utils.helpers")  →  <search_dir>/utils/helpers.lua
---   import("utils.*")        →  load all .lua files under <search_dir>/utils/
---
--- Loaded module results are cached in package.loaded. Repeated imports return the cached result directly.

--- Import a module.
---
--- Single file:  import("dir.sub.mod")   -- load the corresponding .lua file and return the module result.
--- Wildcard dir: import("dir.*")        -- load all .lua files in the directory.
---
---@param module_name string  dot-separated module path (e.g. "utils.helpers") or "dir.*" wildcard
---@return any result  single file: module return value (any type)
---                     wildcard dir: table<filename, result>  key is filename without extension
function import(module_name) end

--- Set the search directory list (replaces all existing paths).
---@param paths string  semicolon-separated directory paths, e.g. "scripts/;mods/"
function import.setpath(paths) end

--- Append a search directory.
---@param path string  directory path, e.g. "extra/"
function import.addpath(path) end

--- Return the package.loaded table, which shows all cached modules.
---@return table loaded  package.loaded
function import.loaded() end

--- Clear the module cache (package.loaded = {}), enabling hot reload.
function import.clearcache() end
