#pragma once

#include <string>
#include "log_init.h"
#include <runtime/vm/vm.h>
#include <catch2/catch_test_macros.hpp>

// Provides an isolated ScriptVM for tests.
// log_init.h starts the quill backend before main(), since ScriptVM's
// constructor calls GetLogger().
struct ScriptVMFixture {
    engine::ScriptVM vm;

    ScriptVMFixture() = default;

    bool RunLua(const std::string& code, std::string* error_out = nullptr) {
        return vm.DoString(code, "test", error_out);
    }

    bool RunLuaCapture(const std::string& code, std::string& result_out) {
        return vm.DoString(code, "test", nullptr, &result_out);
    }

    // Push a value onto the Lua stack and set it as a global.
    void SetGlobalInt(const std::string& name, int value) {
        vm.SetGlobal(name, value);
    }

    void SetGlobalStr(const std::string& name, const std::string& value) {
        vm.SetGlobal<std::string_view>(name, value);
    }

    void SetGlobalBool(const std::string& name, bool value) {
        vm.SetGlobal(name, value);
    }
};
