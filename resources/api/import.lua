--- Module Import API
--- 全局函数: import，函数对象本身同时携带子函数作为字段。
---
--- 模块名使用点号分隔路径，自动映射到文件系统：
---   import("utils.helpers")  →  <search_dir>/utils/helpers.lua
---   import("utils.*")        →  加载 <search_dir>/utils/ 下全部 .lua 文件
---
--- 加载的模块结果缓存在 package.loaded 中，重复 import 直接返回缓存。

--- 导入模块。
---
--- 单文件:  import("dir.sub.mod")   — 加载对应的 .lua 文件并返回模块结果。
--- 通配目录: import("dir.*")        — 加载目录下全部 .lua 文件。
---
---@param module_name string  点号分隔的模块路径 (如 "utils.helpers") 或 "dir.*" 通配
---@return any result  单文件: 模块返回值 (任意类型)
---                     通配目录: table<filename, result>  key 为不含扩展名的文件名
function import(module_name) end

--- 设置搜索目录列表（替换现有全部路径）。
---@param paths string  分号分隔的目录路径，如 "scripts/;mods/"
function import.setpath(paths) end

--- 追加一个搜索目录。
---@param path string  目录路径，如 "extra/"
function import.addpath(path) end

--- 返回 package.loaded 表，可由此查看所有已缓存的模块。
---@return table loaded  package.loaded
function import.loaded() end

--- 清空模块缓存 (package.loaded = {})，支持热重载。
function import.clearcache() end
