#pragma once

namespace engine {
namespace redis {

const char* RedisConnectionErrorString(int status, const char* errstr);

}  // namespace redis
}  // namespace engine

