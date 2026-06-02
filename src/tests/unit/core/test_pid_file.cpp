#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>

#include "runtime/core/pid_file.h"

namespace {

struct TempPidDir {
    std::filesystem::path root;

    TempPidDir() {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("evpp_pid_file_test_" + std::to_string(stamp));
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root, ec);
    }

    ~TempPidDir() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

}  // namespace

TEST_CASE("PidFile creates parent directories and locks exclusively", "[pid_file]") {
    TempPidDir tmp;
    auto pid_path = tmp.root / "instances" / "server-a.pid";

    std::string error;
    REQUIRE(engine::PidFile::WritePidFile(pid_path.string(), &error));
    REQUIRE(std::filesystem::is_regular_file(pid_path));

    std::string lock_error;
    REQUIRE_FALSE(engine::PidFile::WritePidFile(pid_path.string(), &lock_error));
    REQUIRE_FALSE(lock_error.empty());

    engine::PidFile::RemovePidFile(pid_path.string());
    REQUIRE_FALSE(std::filesystem::exists(pid_path));

    error.clear();
    REQUIRE(engine::PidFile::WritePidFile(pid_path.string(), &error));
    engine::PidFile::RemovePidFile(pid_path.string());
}
