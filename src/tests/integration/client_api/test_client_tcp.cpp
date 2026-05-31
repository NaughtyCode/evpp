#include <catch2/catch_test_macros.hpp>
#include <client.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

struct ClientHandle {
    game_client_t* ptr = nullptr;

    ClientHandle() {
        REQUIRE(game_client_create(&ptr) == GAME_OK);
        REQUIRE(ptr != nullptr);
    }

    ~ClientHandle() {
        game_client_destroy(&ptr);
    }
};

struct TempRuntimeDirs {
    std::filesystem::path root;
    std::filesystem::path resource_dir;
    std::filesystem::path runtime_scripts;
    std::filesystem::path client_scripts;
    std::filesystem::path server_scripts;

    TempRuntimeDirs()
        : root(std::filesystem::temp_directory_path() / "evpp_client_api_runtime")
        , resource_dir(root / "resources")
        , runtime_scripts(root / "script" / "runtime")
        , client_scripts(root / "script" / "client")
        , server_scripts(root / "script" / "server") {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(resource_dir / "physics" / "config", ec);
        std::filesystem::create_directories(runtime_scripts, ec);
        std::filesystem::create_directories(client_scripts, ec);
        std::filesystem::create_directories(server_scripts, ec);
    }

    ~TempRuntimeDirs() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    static std::string json_path(const std::filesystem::path& path) {
        return path.generic_string();
    }
};

void load_runtime_config(game_client_t* client, const TempRuntimeDirs& dirs) {
    const std::string runtime_json =
        std::string(R"({"resource_dir":")") + TempRuntimeDirs::json_path(dirs.resource_dir) +
        R"(","log":{"dir":"logs","level":"debug"},"frame":{"target_fps":0,"interval_ms":1},)"
        R"("scripts_dir":")" + TempRuntimeDirs::json_path(dirs.runtime_scripts) +
        R"(","sandbox_level":"full","environment":"development"})";

    const std::string client_json =
        std::string(R"({"scripts_dir":")") + TempRuntimeDirs::json_path(dirs.client_scripts) +
        R"(","render":{"backend":"opengl","max_fps":60},"window":{"title":"Integration"}})";

    const std::string server_json =
        std::string(R"({"http":{"timeout_sec":1.0},"msgpack":{"max_nesting_depth":16,)"
                    R"("max_payload_size":1048576},"scripts_dir":")") +
        TempRuntimeDirs::json_path(dirs.server_scripts) + R"(","admin_port":0})";

    REQUIRE(game_config_load_runtime_json(client, runtime_json.c_str()) == GAME_OK);
    REQUIRE(game_config_load_client_json(client, client_json.c_str()) == GAME_OK);
    REQUIRE(game_config_load_server_json(client, server_json.c_str()) == GAME_OK);
}

}  // namespace

TEST_CASE("Client API handle size is pointer-sized", "[integration][client][abi]") {
    REQUIRE(sizeof(game_client_t*) == sizeof(void*));
}

TEST_CASE("Client API version string is non-empty", "[integration][client][abi]") {
    const char* version = game_version();
    REQUIRE(version != nullptr);
    REQUIRE(std::strlen(version) > 0);
}

TEST_CASE("Client API create and destroy", "[integration][client][lifecycle]") {
    game_client_t* client = nullptr;
    game_error_t rc = game_client_create(&client);
    REQUIRE(rc == GAME_OK);
    REQUIRE(client != nullptr);
    game_client_destroy(&client);
    REQUIRE(client == nullptr);
}

TEST_CASE("Client API error codes are distinct", "[integration][client][abi]") {
    REQUIRE(GAME_OK == 0);
    REQUIRE(GAME_ERR_INVALID_ARG != GAME_OK);
    REQUIRE(GAME_ERR_NETWORK != GAME_OK);
}

TEST_CASE("Client API init exports updated runtime Lua modules", "[integration][client][runtime]") {
    TempRuntimeDirs dirs;
    ClientHandle client;
    load_runtime_config(client.ptr, dirs);

    REQUIRE(game_client_init(client.ptr, nullptr) == GAME_OK);
    REQUIRE(game_client_is_running(client.ptr));

    const char* script = R"(
        assert(json.validate('{"a":1}') == true)
        local packed = cmsgpack.pack({answer = 42})
        local decoded = cmsgpack.unpack(packed)
        assert(decoded.answer == 42)

        auth.set_token_backend()
        auth.add_token('lua-token', 'lua-entity')
        local ok, entity_id, sid = auth.authenticate('token', { token = 'lua-token' })
        assert(ok and entity_id == 'lua-entity')
        assert(auth.validate_session(sid))

        assert(config.get('scripts_dir') ~= nil)
        local e = entity.create()
        e:set_attr('name', 'client')
        assert(e:get_attr('name') == 'client')
        e:destroy()

        local space_id = space.create('client-eval-space', { max_entities = 4, max_players = 1 })
        local info = space.get(space_id)
        assert(info and info.name == 'client-eval-space')
        space.destroy(space_id)

        aoi.init(100, 100, 10)
        aoi.register_entity(1, 0, 0, 20)
        aoi.register_entity(2, 5, 0, 20)
        local visible = aoi.get_visible(1)
        local found = false
        for _, id in ipairs(visible) do
            if id == 2 then found = true end
        end
        assert(found)
        aoi.shutdown()

        assert(type(import.loaded()) == 'table')
        return 'runtime-modules-ok'
    )";

    char out[64];
    char err[512] = {};
    int out_len = 0;
    const game_error_t rc =
        game_client_eval_string(client.ptr, script, out, sizeof(out), &out_len, err, sizeof(err));
    INFO(err);
    REQUIRE(rc == GAME_OK);
    REQUIRE(std::string(out) == "runtime-modules-ok");
}

TEST_CASE("Client API MessagePack JSON bridge round trips through runtime modules",
          "[integration][client][runtime][msgpack]") {
    TempRuntimeDirs dirs;
    ClientHandle client;
    load_runtime_config(client.ptr, dirs);

    REQUIRE(game_client_init(client.ptr, nullptr) == GAME_OK);

    int packed_len = 0;
    REQUIRE(game_msgpack_pack_json(client.ptr, R"({"answer":42,"text":"ok"})", nullptr, 0,
                                   &packed_len) == GAME_OK);
    REQUIRE(packed_len > 0);

    std::vector<uint8_t> packed(static_cast<size_t>(packed_len));
    REQUIRE(game_msgpack_pack_json(client.ptr, R"({"answer":42,"text":"ok"})", packed.data(),
                                   static_cast<int>(packed.size()), &packed_len) == GAME_OK);

    int json_len = 0;
    REQUIRE(game_msgpack_unpack_to_json(client.ptr, packed.data(), packed_len, nullptr, 0,
                                        &json_len) == GAME_OK);
    REQUIRE(json_len > 0);

    std::vector<char> json(static_cast<size_t>(json_len) + 1);
    REQUIRE(game_msgpack_unpack_to_json(client.ptr, packed.data(), packed_len, json.data(),
                                        static_cast<int>(json.size()), &json_len) == GAME_OK);
    const std::string json_text(json.data(), static_cast<size_t>(json_len));
    REQUIRE(json_text.find("\"answer\":42") != std::string::npos);
    REQUIRE(json_text.find("\"text\":\"ok\"") != std::string::npos);
}
