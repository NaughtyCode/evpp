#pragma once

#include <stdint.h>

#include <memcached/protocol_binary.h>

#include "runtime/evpp/buffer.h"
#include "runtime/evpp/tcp_conn.h"
#include "runtime/evpp/timestamp.h"

#include "tests/apps/evmc/command.h"

namespace evmc {

class MemcacheClient;

class BinaryCodec {
public:
    explicit BinaryCodec(MemcacheClient* memc_client) : memc_client_(memc_client) {}

    void OnCodecMessage(const evpp::TCPConnPtr& conn,
                        evpp::Buffer* buf);

private:
    // noncopyable
    BinaryCodec(const BinaryCodec&);
    void DecodePrefixGetPacket(const protocol_binary_response_header& resp,
                               evpp::Buffer* buf, std::string& key, CommandPtr& cmd);
    const BinaryCodec& operator=(const BinaryCodec&);

    void OnResponsePacket(const protocol_binary_response_header& resp,
                          evpp::Buffer* buf);
private:
    // TODO: if using smart pointers, handle circular references. client callback references codec
    MemcacheClient* memc_client_;
};

}

