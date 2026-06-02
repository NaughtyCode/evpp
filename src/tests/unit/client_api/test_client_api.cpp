#include <catch2/catch_test_macros.hpp>

extern "C" {
#include "client/client.h"
}

#include <algorithm>
#include <cstdint>
#include <cstring>
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

const char* kRuntimeJson = R"({
    "resource_dir": "resources",
    "log": { "dir": "logs", "level": "debug" },
    "frame": { "target_fps": 30, "interval_ms": 33 },
    "scripts_dir": "resources/script/runtime",
    "sandbox_level": "full",
    "environment": "development"
})";

const char* kClientJson = R"({
    "scripts_dir": "resources/script/client",
    "render": {
        "backend": "opengl",
        "resolution_width": 1600,
        "resolution_height": 900,
        "vsync": false
    },
    "window": {
        "title": "Unit Client",
        "width": 1024,
        "height": 768,
        "resizable": false
    },
    "audio": {
        "master_volume": 0.5
    },
    "network": {
        "server_address": "10.0.0.1",
        "server_port": 1234
    },
    "ui": {
        "locale": "zh_CN"
    }
})";

const char* kServerJson = R"({
    "http": { "timeout_sec": 7.5 },
    "msgpack": {
        "max_nesting_depth": 16,
        "max_payload_size": 1048576
    },
    "scripts_dir": "resources/script/server",
    "admin_port": 0
})";

void load_basic_config(game_client_t* client) {
    REQUIRE(game_config_load_runtime_json(client, kRuntimeJson) == GAME_OK);
    REQUIRE(game_config_load_client_json(client, kClientJson) == GAME_OK);
    REQUIRE(game_config_load_server_json(client, kServerJson) == GAME_OK);
}

bool contains_id(const uint64_t* ids, int count, uint64_t id) {
    return std::find(ids, ids + count, id) != ids + count;
}

}  // namespace

TEST_CASE("client_api: ABI handle pointer size", "[client_api]") {
    // game_client_t is an opaque handle; verify pointer type is well-formed.
    STATIC_CHECK(sizeof(game_client_t*) == sizeof(void*));
}

TEST_CASE("client_api: version string", "[client_api]") {
    const char* ver = game_version();
    REQUIRE(ver != nullptr);
    REQUIRE(std::strlen(ver) > 0);
}

TEST_CASE("client_api: error code values", "[client_api]") {
    STATIC_CHECK(GAME_OK == 0);
    STATIC_CHECK(GAME_ERR_INVALID_ARG < 0);
    STATIC_CHECK(GAME_ERR_NETWORK < 0);
    STATIC_CHECK(GAME_ERR_BUFFER_TOO_SMALL < 0);
}

TEST_CASE("client_api: create and destroy", "[client_api]") {
    game_client_t* c = nullptr;
    game_error_t rc = game_client_create(&c);
    REQUIRE(rc == GAME_OK);
    REQUIRE(c != nullptr);
    game_client_destroy(&c);
    REQUIRE(c == nullptr);
}

TEST_CASE("client_api: config JSON load, typed getters, dump, and validation", "[client_api][config]") {
    ClientHandle client;
    load_basic_config(client.ptr);

    char text[128];
    int len = 0;
    REQUIRE(game_config_get_string(client.ptr, "resource_dir", text, sizeof(text), &len) == GAME_OK);
    REQUIRE(std::string(text) == "resources");
    REQUIRE(len == 9);

    REQUIRE(game_config_get_string(client.ptr, "client.render.backend", text, sizeof(text), &len) == GAME_OK);
    REQUIRE(std::string(text) == "opengl");

    int64_t int_value = 0;
    REQUIRE(game_config_get_int(client.ptr, "frame.target_fps", &int_value) == GAME_OK);
    REQUIRE(int_value == 30);
    REQUIRE(game_config_get_int(client.ptr, "client.network.server_port", &int_value) == GAME_OK);
    REQUIRE(int_value == 1234);

    double double_value = 0.0;
    REQUIRE(game_config_get_double(client.ptr, "server.http.timeout_sec", &double_value) == GAME_OK);
    REQUIRE(double_value == 7.5);
    REQUIRE(game_config_get_double(client.ptr, "client.audio.master_volume", &double_value) == GAME_OK);
    REQUIRE(double_value == 0.5);

    bool bool_value = true;
    REQUIRE(game_config_get_bool(client.ptr, "client.window.resizable", &bool_value) == GAME_OK);
    REQUIRE_FALSE(bool_value);

    REQUIRE(game_config_apply_client_overrides_json(
                client.ptr, R"({"window":{"width":1111},"ui":{"theme":"light"}})") == GAME_OK);
    REQUIRE(game_config_get_int(client.ptr, "client.window.width", &int_value) == GAME_OK);
    REQUIRE(int_value == 1111);
    REQUIRE(game_config_get_string(client.ptr, "client.ui.theme", text, sizeof(text), &len) == GAME_OK);
    REQUIRE(std::string(text) == "light");

    char errors[256];
    char warnings[256];
    REQUIRE(game_config_validate(client.ptr, errors, sizeof(errors), warnings, sizeof(warnings)));
    REQUIRE(std::strlen(errors) == 0);

    int dump_len = 0;
    REQUIRE(game_config_dump(client.ptr, nullptr, 0, &dump_len) == GAME_OK);
    REQUIRE(dump_len > 0);
    std::vector<char> dump(static_cast<size_t>(dump_len) + 1);
    REQUIRE(game_config_dump(client.ptr, dump.data(), static_cast<int>(dump.size()), &dump_len) == GAME_OK);
    REQUIRE(std::string(dump.data()).find("\"client\"") != std::string::npos);

    REQUIRE(game_config_get_string(client.ptr, "client.no_such_path", text, sizeof(text), &len) ==
            GAME_ERR_NOT_FOUND);
}

TEST_CASE("client_api: JSON helpers validate, minify, prettify, and report buffer size", "[client_api][json]") {
    ClientHandle client;

    char error[256];
    REQUIRE(game_json_validate(client.ptr, R"({"a":1,"b":[true,false]})", false, error, sizeof(error)) ==
            GAME_OK);
    REQUIRE(std::strlen(error) == 0);

    REQUIRE(game_json_validate(client.ptr, R"({"a":)", false, error, sizeof(error)) ==
            GAME_ERR_SCRIPT);
    REQUIRE(std::strlen(error) > 0);

    int len = 0;
    REQUIRE(game_json_minify(client.ptr, "{\n  \"a\": 1,\n  \"b\": true\n}", false, nullptr, 0, &len) ==
            GAME_OK);
    REQUIRE(len == 16);

    char tiny[4];
    REQUIRE(game_json_minify(client.ptr, "{ \"a\" : 1 }", false, tiny, sizeof(tiny), &len) ==
            GAME_ERR_BUFFER_TOO_SMALL);

    std::vector<char> minified(static_cast<size_t>(len) + 1);
    REQUIRE(game_json_minify(client.ptr, "{ \"a\" : 1 }", false, minified.data(),
                             static_cast<int>(minified.size()), &len) == GAME_OK);
    REQUIRE(std::string(minified.data()) == R"({"a":1})");

    std::vector<char> pretty(128);
    REQUIRE(game_json_prettify(client.ptr, R"({"a":1})", false, pretty.data(),
                               static_cast<int>(pretty.size()), &len) == GAME_OK);
    REQUIRE(std::string(pretty.data()).find('\n') != std::string::npos);
}

TEST_CASE("client_api: auth token backend and permissions", "[client_api][auth]") {
    ClientHandle client;

    REQUIRE(game_auth_set_token_backend(client.ptr) == GAME_OK);
    REQUIRE(game_auth_add_token(client.ptr, "unit-token", "entity-1") == GAME_OK);
    REQUIRE(game_auth_grant_permission(client.ptr, "entity-1", "entity:read") == GAME_OK);
    REQUIRE(game_auth_has_permission(client.ptr, "entity-1", "entity:read"));

    char entity_id[64];
    char session_id[128];
    REQUIRE(game_auth_authenticate_token(
                client.ptr, "unit-token", entity_id, sizeof(entity_id), session_id, sizeof(session_id)) ==
            GAME_OK);
    REQUIRE(std::string(entity_id) == "entity-1");
    REQUIRE(std::strlen(session_id) > 0);
    REQUIRE(game_auth_validate_session(client.ptr, session_id));

    REQUIRE(game_auth_revoke_permission(client.ptr, "entity-1", "entity:read") == GAME_OK);
    REQUIRE_FALSE(game_auth_has_permission(client.ptr, "entity-1", "entity:read"));

    REQUIRE(game_auth_revoke_session(client.ptr, session_id) == GAME_OK);
    REQUIRE_FALSE(game_auth_validate_session(client.ptr, session_id));
    REQUIRE(game_auth_authenticate_token(
                client.ptr, "missing", entity_id, sizeof(entity_id), session_id, sizeof(session_id)) ==
            GAME_ERR_NOT_FOUND);
}

TEST_CASE("client_api: metrics counters gauges histograms and exports", "[client_api][metrics]") {
    ClientHandle client;

    REQUIRE(game_metrics_counter_inc(client.ptr, "client_api_unit_counter_total", 3) == GAME_OK);
    REQUIRE(game_metrics_gauge_set(client.ptr, "client_api_unit_gauge", 10) == GAME_OK);
    REQUIRE(game_metrics_gauge_inc(client.ptr, "client_api_unit_gauge", -2) == GAME_OK);
    REQUIRE(game_metrics_histogram_observe(client.ptr, "client_api_unit_latency_ms", 12.5) == GAME_OK);

    int len = 0;
    REQUIRE(game_metrics_export_prometheus(client.ptr, nullptr, 0, &len) == GAME_OK);
    std::vector<char> prometheus(static_cast<size_t>(len) + 1);
    REQUIRE(game_metrics_export_prometheus(client.ptr, prometheus.data(),
                                           static_cast<int>(prometheus.size()), &len) == GAME_OK);
    const std::string prometheus_text(prometheus.data(), static_cast<size_t>(len));
    REQUIRE(prometheus_text.find("client_api_unit_counter_total 3") != std::string::npos);
    REQUIRE(prometheus_text.find("client_api_unit_latency_ms_count 1") != std::string::npos);

    REQUIRE(game_metrics_export_json(client.ptr, nullptr, 0, &len) == GAME_OK);
    std::vector<char> json(static_cast<size_t>(len) + 1);
    REQUIRE(game_metrics_export_json(client.ptr, json.data(), static_cast<int>(json.size()), &len) ==
            GAME_OK);
    REQUIRE(std::string(json.data()).find("\"client_api_unit_gauge\":8") != std::string::npos);
}

TEST_CASE("client_api: entity lifecycle and typed attributes", "[client_api][entity]") {
    ClientHandle client;

    uint64_t before = 0;
    REQUIRE(game_entity_count(client.ptr, &before) == GAME_OK);

    uint64_t entity_id = 0;
    REQUIRE(game_entity_create(client.ptr, 0, &entity_id) == GAME_OK);
    REQUIRE(entity_id != 0);
    REQUIRE(game_entity_exists(client.ptr, entity_id));

    uint64_t count = 0;
    REQUIRE(game_entity_count(client.ptr, &count) == GAME_OK);
    REQUIRE(count == before + 1);

    REQUIRE(game_entity_set_attr_string(client.ptr, entity_id, "name", "hero") == GAME_OK);
    REQUIRE(game_entity_set_attr_int(client.ptr, entity_id, "level", 7) == GAME_OK);
    REQUIRE(game_entity_set_attr_double(client.ptr, entity_id, "speed", 3.5) == GAME_OK);
    REQUIRE(game_entity_set_attr_bool(client.ptr, entity_id, "alive", true) == GAME_OK);
    REQUIRE(game_entity_attr_count(client.ptr, entity_id, &count) == GAME_OK);
    REQUIRE(count == 4);

    char value[64];
    int len = 0;
    REQUIRE(game_entity_get_attr_string(client.ptr, entity_id, "name", value, sizeof(value), &len) ==
            GAME_OK);
    REQUIRE(std::string(value) == "hero");

    int64_t int_value = 0;
    REQUIRE(game_entity_get_attr_int(client.ptr, entity_id, "level", &int_value) == GAME_OK);
    REQUIRE(int_value == 7);

    double double_value = 0.0;
    REQUIRE(game_entity_get_attr_double(client.ptr, entity_id, "speed", &double_value) == GAME_OK);
    REQUIRE(double_value == 3.5);

    bool bool_value = false;
    REQUIRE(game_entity_get_attr_bool(client.ptr, entity_id, "alive", &bool_value) == GAME_OK);
    REQUIRE(bool_value);

    REQUIRE(game_entity_remove_attr(client.ptr, entity_id, "name") == GAME_OK);
    REQUIRE(game_entity_get_attr_string(client.ptr, entity_id, "name", value, sizeof(value), &len) ==
            GAME_ERR_NOT_FOUND);

    REQUIRE(game_entity_destroy(client.ptr, entity_id) == GAME_OK);
    REQUIRE_FALSE(game_entity_exists(client.ptr, entity_id));
}

TEST_CASE("client_api: space lifecycle", "[client_api][space]") {
    ClientHandle client;

    uint64_t before = 0;
    REQUIRE(game_space_count(client.ptr, &before) == GAME_OK);

    uint64_t space_id = 0;
    REQUIRE(game_space_create(client.ptr, "unit-space", 8, 2, &space_id) == GAME_OK);
    REQUIRE(space_id != 0);

    uint64_t count = 0;
    REQUIRE(game_space_count(client.ptr, &count) == GAME_OK);
    REQUIRE(count == before + 1);

    REQUIRE(game_space_destroy(client.ptr, space_id) == GAME_OK);
    REQUIRE(game_space_count(client.ptr, &count) == GAME_OK);
    REQUIRE(count == before);
}

TEST_CASE("client_api: AOI register move query visible and unregister", "[client_api][aoi]") {
    ClientHandle client;

    game_aoi_t* aoi = nullptr;
    REQUIRE(game_aoi_create(client.ptr, 200.0f, 200.0f, 20.0f, &aoi) == GAME_OK);
    REQUIRE(aoi != nullptr);

    REQUIRE(game_aoi_register_entity(aoi, 1, 0.0f, 0.0f, 25.0f) == GAME_OK);
    REQUIRE(game_aoi_register_entity(aoi, 2, 10.0f, 0.0f, 25.0f) == GAME_OK);
    REQUIRE(game_aoi_register_entity(aoi, 3, 150.0f, 150.0f, 25.0f) == GAME_OK);

    uint64_t count = 0;
    REQUIRE(game_aoi_count(aoi, &count) == GAME_OK);
    REQUIRE(count == 3);

    uint64_t ids[4] = {};
    int id_count = 0;
    REQUIRE(game_aoi_query_radius(aoi, 0.0f, 0.0f, 30.0f, ids, 4, &id_count) == GAME_OK);
    REQUIRE(contains_id(ids, id_count, 1));
    REQUIRE(contains_id(ids, id_count, 2));
    REQUIRE_FALSE(contains_id(ids, id_count, 3));

    REQUIRE(game_aoi_get_visible(aoi, 1, ids, 4, &id_count) == GAME_OK);
    REQUIRE(contains_id(ids, id_count, 2));

    REQUIRE(game_aoi_move_entity(aoi, 2, 180.0f, 180.0f) == GAME_OK);
    REQUIRE(game_aoi_get_visible(aoi, 1, ids, 4, &id_count) == GAME_OK);
    REQUIRE_FALSE(contains_id(ids, id_count, 2));

    REQUIRE(game_aoi_unregister_entity(aoi, 3) == GAME_OK);
    REQUIRE(game_aoi_count(aoi, &count) == GAME_OK);
    REQUIRE(count == 2);

    game_aoi_destroy(&aoi);
    REQUIRE(aoi == nullptr);
}

TEST_CASE("client_api: AOI boundary contracts", "[client_api][aoi]") {
    ClientHandle client;

    game_aoi_t* aoi = nullptr;
    REQUIRE(game_aoi_create(client.ptr, 500.0f, 500.0f, 50.0f, &aoi) == GAME_OK);
    REQUIRE(aoi != nullptr);

    REQUIRE(game_aoi_register_entity(aoi, 1, 100.0f, 100.0f, 100.0f) == GAME_OK);
    REQUIRE(game_aoi_register_entity(aoi, 2, 150.0f, 100.0f, 1.0f) == GAME_OK);
    REQUIRE(game_aoi_register_entity(aoi, 3, 300.0f, 100.0f, 10.0f) == GAME_OK);

    uint64_t ids[4] = {};
    int id_count = 0;

    REQUIRE(game_aoi_get_visible(aoi, 1, nullptr, 0, &id_count) == GAME_OK);
    REQUIRE(id_count == 1);

    REQUIRE(game_aoi_get_visible(aoi, 2, ids, 4, &id_count) == GAME_OK);
    REQUIRE_FALSE(contains_id(ids, id_count, 1));

    REQUIRE(game_aoi_query_radius(aoi, 100.0f, 100.0f, 0.0f, ids, 4, &id_count) == GAME_OK);
    REQUIRE(id_count == 1);
    REQUIRE(contains_id(ids, id_count, 1));

    REQUIRE(game_aoi_query_radius(aoi, 125.0f, 100.0f, 100.0f, nullptr, 0, &id_count) ==
            GAME_OK);
    REQUIRE(id_count == 2);

    uint64_t tiny[1] = {};
    REQUIRE(game_aoi_query_radius(aoi, 125.0f, 100.0f, 100.0f, tiny, 1, &id_count) ==
            GAME_ERR_BUFFER_TOO_SMALL);
    REQUIRE(id_count == 2);

    REQUIRE(game_aoi_query_radius(aoi, 125.0f, 100.0f, 100.0f, nullptr, 0, nullptr) ==
            GAME_ERR_INVALID_ARG);
    REQUIRE(game_aoi_query_radius(aoi, 125.0f, 100.0f, 100.0f, ids, -1, &id_count) ==
            GAME_ERR_INVALID_ARG);
    REQUIRE(game_aoi_get_visible(aoi, 1, nullptr, 0, nullptr) == GAME_ERR_INVALID_ARG);
    REQUIRE(game_aoi_get_visible(aoi, 1, ids, -1, &id_count) == GAME_ERR_INVALID_ARG);

    REQUIRE(game_aoi_register_entity(aoi, 1, 300.0f, 100.0f, 20.0f) == GAME_OK);
    uint64_t count = 0;
    REQUIRE(game_aoi_count(aoi, &count) == GAME_OK);
    REQUIRE(count == 3);
    REQUIRE(game_aoi_query_radius(aoi, 100.0f, 100.0f, 1.0f, ids, 4, &id_count) == GAME_OK);
    REQUIRE_FALSE(contains_id(ids, id_count, 1));
    REQUIRE(game_aoi_query_radius(aoi, 300.0f, 100.0f, 1.0f, ids, 4, &id_count) == GAME_OK);
    REQUIRE(contains_id(ids, id_count, 1));

    REQUIRE(game_aoi_update_radius(aoi, 1, 250.0f) == GAME_OK);
    REQUIRE(game_aoi_get_visible(aoi, 1, ids, 4, &id_count) == GAME_OK);
    REQUIRE(contains_id(ids, id_count, 2));
    REQUIRE(contains_id(ids, id_count, 3));

    REQUIRE(game_aoi_update_radius(aoi, 1, -1.0f) == GAME_ERR_INVALID_ARG);
    REQUIRE(game_aoi_count(nullptr, &count) == GAME_ERR_INVALID_ARG);

    game_aoi_destroy(&aoi);
    REQUIRE(aoi == nullptr);
}

TEST_CASE("client_api: AOI oversized grid creation returns an error code", "[client_api][aoi]") {
    ClientHandle client;

    game_aoi_t* aoi = reinterpret_cast<game_aoi_t*>(static_cast<uintptr_t>(1));
    REQUIRE(game_aoi_create(client.ptr, -1.0f, 1000.0f, 1.0f, &aoi) == GAME_ERR_INVALID_ARG);
    REQUIRE(aoi == nullptr);

    aoi = reinterpret_cast<game_aoi_t*>(static_cast<uintptr_t>(1));
    REQUIRE(game_aoi_create(client.ptr, 1.0e20f, 1000.0f, 1.0f, &aoi) == GAME_ERR_GENERIC);
    REQUIRE(aoi == nullptr);
}
