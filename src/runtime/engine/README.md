# engine

游戏服务器引擎。

## 开发语言

- C++17

## 代码风格

- 使用 `clang-format`（项目根目录 `.clang-format`）
- 类型名 `PascalCase`，函数名 `PascalCase`，变量名 `snake_case`，常量 `kPascalCase`
- 成员变量后缀 `_`（如 `player_id_`）
- 头文件 include 顺序：自身头文件 → 标准库 → 第三方库 → 项目头文件
- 每个 `.cc` 配对 `.h`，统一使用 `#pragma once`
- 禁止裸 `new/delete`，用 `std::unique_ptr`/`std::shared_ptr` 管理生命周期

## 提交规范

- 分支命名：`feature/<描述>`、`fix/<描述>`、`refactor/<描述>`
- commit message：英文、首行不超过 72 字符，格式 `<type>: <概述>`
  - `feat:` 新功能
  - `fix:` 修复
  - `refactor:` 重构
  - `docs:` 文档
  - `test:` 测试
- 一个 commit 只做一件事，不混入无关改动
