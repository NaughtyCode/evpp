#include "runtime/auth/auth_backend.h"

#include <array>
#include <cstring>
#include <random>
#include <sstream>
#include <vector>

#include "runtime/core/log/log.h"
#include "runtime/profiler/profiler_events.h"

namespace engine {
namespace auth {

// =============================================================================
// Minimal SHA-256 implementation (FIPS 180-4)
// =============================================================================

namespace {

constexpr uint32_t kSha256Init[] = {
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

constexpr uint32_t kSha256K[] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t rotr32(uint32_t x, uint32_t n) {
	return (x >> n) | (x << (32 - n));
}

void Sha256Transform(uint32_t* state, const uint8_t* block) {
	uint32_t w[64];
	for (int i = 0; i < 16; ++i) {
		w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
		       (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
		       (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
		       (static_cast<uint32_t>(block[i * 4 + 3]));
	}
	for (int i = 16; i < 64; ++i) {
		uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
	uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

	for (int i = 0; i < 64; ++i) {
		uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
		uint32_t ch = (e & f) ^ ((~e) & g);
		uint32_t t1 = h + S1 + ch + kSha256K[i] + w[i];
		uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
		uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
		uint32_t t2 = S0 + maj;

		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}

	state[0] += a; state[1] += b; state[2] += c; state[3] += d;
	state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

std::string Sha256(const std::string& data) {
	uint32_t state[8];
	std::memcpy(state, kSha256Init, sizeof(state));

	std::vector<uint8_t> padded(data.begin(), data.end());
	uint64_t bitlen = padded.size() * 8;
	padded.push_back(0x80);
	while ((padded.size() % 64) != 56) {
		padded.push_back(0x00);
	}
	for (int i = 7; i >= 0; --i) {
		padded.push_back(static_cast<uint8_t>((bitlen >> (i * 8)) & 0xff));
	}

	for (size_t i = 0; i < padded.size(); i += 64) {
		Sha256Transform(state, padded.data() + i);
	}

	std::string hash(32, '\0');
	for (int i = 0; i < 8; ++i) {
		hash[i * 4]     = static_cast<char>((state[i] >> 24) & 0xff);
		hash[i * 4 + 1] = static_cast<char>((state[i] >> 16) & 0xff);
		hash[i * 4 + 2] = static_cast<char>((state[i] >> 8) & 0xff);
		hash[i * 4 + 3] = static_cast<char>(state[i] & 0xff);
	}
	return hash;
}

// HMAC-SHA256 (RFC 2104)
std::string HmacSha256(const std::string& key, const std::string& message) {
	constexpr size_t kBlockSize = 64;
	std::string key_block = key;
	if (key_block.size() > kBlockSize) {
		key_block = Sha256(key_block);
	}
	key_block.resize(kBlockSize, '\0');

	std::string o_key_pad(kBlockSize, '\0');
	std::string i_key_pad(kBlockSize, '\0');
	for (size_t i = 0; i < kBlockSize; ++i) {
		o_key_pad[i] = static_cast<char>(key_block[i] ^ 0x5c);
		i_key_pad[i] = static_cast<char>(key_block[i] ^ 0x36);
	}

	return Sha256(o_key_pad + Sha256(i_key_pad + message));
}

}  // namespace

// =============================================================================
// AuthBackend — permission checking
// =============================================================================

bool AuthBackend::HasPermission(const std::string& entity_id,
                                 const std::string& permission) {
	auto it = entity_permissions_.find(entity_id);
	if (it == entity_permissions_.end()) return false;
	return it->second.count(permission) > 0;
}

void AuthBackend::GrantPermission(const std::string& entity_id,
                                   const std::string& permission) {
	entity_permissions_[entity_id].insert(permission);
}

void AuthBackend::RevokePermission(const std::string& entity_id,
                                    const std::string& permission) {
	auto it = entity_permissions_.find(entity_id);
	if (it != entity_permissions_.end()) {
		it->second.erase(permission);
	}
}

// =============================================================================
// TokenAuthBackend
// =============================================================================

void TokenAuthBackend::AddToken(const std::string& token, const std::string& entity_id) {
	tokens_[token] = entity_id;
}

AuthResult TokenAuthBackend::Authenticate(const std::string& method,
                                           const std::map<std::string, std::string>& params) {
	ENGINE_PROFILE_AUTH_AUTHENTICATE();
	AuthResult result;

	if (method != "token") {
		result.reason = "unsupported method: " + method;
		return result;
	}

	auto it = params.find("token");
	if (it == params.end()) {
		result.reason = "missing token parameter";
		return result;
	}

	auto token_it = tokens_.find(it->second);
	if (token_it == tokens_.end()) {
		result.reason = "invalid token";
		return result;
	}

	// Generate a session ID
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<uint64_t> dist;
	std::ostringstream ss;
	ss << std::hex << dist(gen) << dist(gen);
	std::string session_id = ss.str();

	sessions_[session_id] = token_it->second;

	result.success = true;
	result.entity_id = token_it->second;
	result.session_id = session_id;
	result.expires_at = 0;

	// Copy entity permissions
	auto perm_it = entity_permissions_.find(token_it->second);
	if (perm_it != entity_permissions_.end()) {
		result.permissions = perm_it->second;
	}

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "TokenAuth: entity [{}] authenticated, session=[{}]",
	                result.entity_id, session_id);

	return result;
}

bool TokenAuthBackend::ValidateSession(const std::string& session_id) {
	ENGINE_PROFILE_AUTH_VALIDATE();
	return sessions_.find(session_id) != sessions_.end();
}

void TokenAuthBackend::RevokeSession(const std::string& session_id) {
	sessions_.erase(session_id);
}

// =============================================================================
// JwtAuthBackend
// =============================================================================

void JwtAuthBackend::SetSecret(const std::string& secret) {
	secret_ = secret;
}

std::string JwtAuthBackend::Base64UrlDecode(std::string_view input) {
	std::string decoded;
	decoded.reserve(input.size() * 3 / 4);

	static const char kDecodeTable[256] = {
		-1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,62,-1,62,-1,63,
		52,53,54,55,56,57,58,59, 60,61,-1,-1,-1, 0,-1,-1,
		-1, 0, 1, 2, 3, 4, 5, 6,  7, 8, 9,10,11,12,13,14,
		15,16,17,18,19,20,21,22, 23,24,25,-1,-1,-1,-1,63,
		-1,26,27,28,29,30,31,32, 33,34,35,36,37,38,39,40,
		41,42,43,44,45,46,47,48, 49,50,51,-1,-1,-1,-1,-1,
	};

	int val = 0, valb = -8;
	for (char c : input) {
		if (c == '=') break;
		int8_t idx = kDecodeTable[static_cast<uint8_t>(c)];
		if (idx < 0) continue;
		val = (val << 6) + idx;
		valb += 6;
		if (valb >= 0) {
			decoded.push_back(static_cast<char>((val >> valb) & 0xff));
			valb -= 8;
		}
	}
	return decoded;
}

std::string JwtAuthBackend::VerifyToken(const std::string& token) {
	// Split into header.payload.signature
	auto pos1 = token.find('.');
	if (pos1 == std::string::npos) return {};
	auto pos2 = token.find('.', pos1 + 1);
	if (pos2 == std::string::npos) return {};

	std::string header_b64 = token.substr(0, pos1);
	std::string payload_b64 = token.substr(pos1 + 1, pos2 - pos1 - 1);
	std::string signature_b64 = token.substr(pos2 + 1);

	// Verify signature using HMAC-SHA256
	if (!secret_.empty()) {
		std::string expected_sig = HmacSha256(secret_, header_b64 + "." + payload_b64);

		// Base64url-encode the expected signature for comparison
		static const char kBase64Url[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
		std::string expected_b64;
		for (size_t i = 0; i < expected_sig.size(); i += 3) {
			uint32_t triple = static_cast<uint8_t>(expected_sig[i]) << 16;
			if (i + 1 < expected_sig.size())
				triple |= static_cast<uint8_t>(expected_sig[i + 1]) << 8;
			if (i + 2 < expected_sig.size())
				triple |= static_cast<uint8_t>(expected_sig[i + 2]);
			expected_b64 += kBase64Url[(triple >> 18) & 0x3f];
			expected_b64 += kBase64Url[(triple >> 12) & 0x3f];
			expected_b64 += (i + 1 < expected_sig.size()) ? kBase64Url[(triple >> 6) & 0x3f] : '=';
			expected_b64 += (i + 2 < expected_sig.size()) ? kBase64Url[triple & 0x3f] : '=';
		}

		if (expected_b64 != signature_b64) return {};
	}

	// Decode payload
	return Base64UrlDecode(payload_b64);
}

AuthResult JwtAuthBackend::Authenticate(const std::string& method,
                                         const std::map<std::string, std::string>& params) {
	ENGINE_PROFILE_AUTH_AUTHENTICATE();
	AuthResult result;

	if (method != "jwt") {
		result.reason = "unsupported method: " + method;
		return result;
	}

	auto it = params.find("token");
	if (it == params.end()) {
		result.reason = "missing token parameter";
		return result;
	}

	std::string payload = VerifyToken(it->second);
	if (payload.empty()) {
		result.reason = "invalid jwt token";
		return result;
	}

	// Extract "sub" claim from JSON payload (simple extraction)
	auto sub_pos = payload.find("\"sub\"");
	if (sub_pos == std::string::npos) {
		result.reason = "missing sub claim in jwt";
		return result;
	}

	auto colon = payload.find(':', sub_pos);
	if (colon == std::string::npos) {
		result.reason = "malformed sub claim";
		return result;
	}

	auto start = payload.find('"', colon);
	if (start == std::string::npos) {
		result.reason = "malformed sub claim";
		return result;
	}

	auto end = payload.find('"', start + 1);
	if (end == std::string::npos) {
		result.reason = "malformed sub claim";
		return result;
	}

	std::string entity_id = payload.substr(start + 1, end - start - 1);

	// Extract expiry "exp" claim
	int64_t now = static_cast<int64_t>(
		std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());

	auto exp_pos = payload.find("\"exp\"");
	if (exp_pos != std::string::npos) {
		auto exp_colon = payload.find(':', exp_pos);
		if (exp_colon != std::string::npos) {
			int64_t exp = 0;
			for (size_t i = exp_colon + 1; i < payload.size(); ++i) {
				if (payload[i] >= '0' && payload[i] <= '9') {
					exp = exp * 10 + (payload[i] - '0');
				} else {
					break;
				}
			}
			if (exp > 0 && now > exp) {
				result.reason = "jwt token expired";
				return result;
			}
		}
	}

	// Create session
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<uint64_t> dist;
	std::ostringstream ss;
	ss << std::hex << dist(gen) << dist(gen);
	std::string session_id = ss.str();

	SessionInfo info;
	info.session_id = session_id;
	info.entity_id = entity_id;
	info.created_at = now;
	info.expires_at = 0;
	info.auth_method = "jwt";

	auto perm_it = entity_permissions_.find(entity_id);
	if (perm_it != entity_permissions_.end()) {
		info.permissions = perm_it->second;
	}

	sessions_[session_id] = info;

	result.success = true;
	result.entity_id = entity_id;
	result.session_id = session_id;
	result.expires_at = 0;
	result.permissions = info.permissions;

	auto* logger = GetLogger();
	ENGINE_LOG_INFO(logger, "JwtAuth: entity [{}] authenticated via JWT, session=[{}]",
	                entity_id, session_id);

	return result;
}

bool JwtAuthBackend::ValidateSession(const std::string& session_id) {
	ENGINE_PROFILE_AUTH_VALIDATE();
	auto it = sessions_.find(session_id);
	if (it == sessions_.end()) return false;

	if (it->second.expires_at > 0) {
		auto now = static_cast<int64_t>(
			std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		if (now > it->second.expires_at) return false;
	}
	return true;
}

void JwtAuthBackend::RevokeSession(const std::string& session_id) {
	sessions_.erase(session_id);
}

}  // namespace auth
}  // namespace engine
