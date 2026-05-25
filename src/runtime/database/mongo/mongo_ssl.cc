#include "runtime/database/mongo/mongo_ssl.h"

#include <mongoc/mongoc.h>

#include <cstring>

namespace engine {
namespace mongo {

struct MongoSslOpts::Impl {
    mongoc_ssl_opt_t opts;
    std::string pem_file;
    std::string pem_pwd;
    std::string ca_file;
    std::string ca_dir;
    std::string crl_file;
};

MongoSslOpts::MongoSslOpts() : impl_(std::make_unique<Impl>()) {
    memset(&impl_->opts, 0, sizeof(impl_->opts));
}

MongoSslOpts::~MongoSslOpts() = default;

MongoSslOpts::MongoSslOpts(const MongoSslOpts& other) : impl_(std::make_unique<Impl>()) {
    impl_->pem_file = other.impl_->pem_file;
    impl_->pem_pwd = other.impl_->pem_pwd;
    impl_->ca_file = other.impl_->ca_file;
    impl_->ca_dir = other.impl_->ca_dir;
    impl_->crl_file = other.impl_->crl_file;
    memcpy(&impl_->opts, &other.impl_->opts, sizeof(impl_->opts));
    if (!impl_->pem_file.empty()) impl_->opts.pem_file = impl_->pem_file.c_str();
    if (!impl_->pem_pwd.empty()) impl_->opts.pem_pwd = impl_->pem_pwd.c_str();
    if (!impl_->ca_file.empty()) impl_->opts.ca_file = impl_->ca_file.c_str();
    if (!impl_->ca_dir.empty()) impl_->opts.ca_dir = impl_->ca_dir.c_str();
    if (!impl_->crl_file.empty()) impl_->opts.crl_file = impl_->crl_file.c_str();
}

MongoSslOpts& MongoSslOpts::operator=(const MongoSslOpts& other) {
    if (this != &other) {
        impl_->pem_file = other.impl_->pem_file;
        impl_->pem_pwd = other.impl_->pem_pwd;
        impl_->ca_file = other.impl_->ca_file;
        impl_->ca_dir = other.impl_->ca_dir;
        impl_->crl_file = other.impl_->crl_file;
        memcpy(&impl_->opts, &other.impl_->opts, sizeof(impl_->opts));
        if (!impl_->pem_file.empty()) impl_->opts.pem_file = impl_->pem_file.c_str();
        if (!impl_->pem_pwd.empty()) impl_->opts.pem_pwd = impl_->pem_pwd.c_str();
        if (!impl_->ca_file.empty()) impl_->opts.ca_file = impl_->ca_file.c_str();
        if (!impl_->ca_dir.empty()) impl_->opts.ca_dir = impl_->ca_dir.c_str();
        if (!impl_->crl_file.empty()) impl_->opts.crl_file = impl_->crl_file.c_str();
    }
    return *this;
}

MongoSslOpts::MongoSslOpts(MongoSslOpts&&) noexcept = default;
MongoSslOpts& MongoSslOpts::operator=(MongoSslOpts&&) noexcept = default;

void MongoSslOpts::SetPemFile(const char* f) {
    impl_->pem_file = f ? f : "";
    impl_->opts.pem_file = f ? impl_->pem_file.c_str() : nullptr;
}

void MongoSslOpts::SetPemPwd(const char* p) {
    impl_->pem_pwd = p ? p : "";
    impl_->opts.pem_pwd = p ? impl_->pem_pwd.c_str() : nullptr;
}

void MongoSslOpts::SetCaFile(const char* f) {
    impl_->ca_file = f ? f : "";
    impl_->opts.ca_file = f ? impl_->ca_file.c_str() : nullptr;
}

void MongoSslOpts::SetCaDir(const char* d) {
    impl_->ca_dir = d ? d : "";
    impl_->opts.ca_dir = d ? impl_->ca_dir.c_str() : nullptr;
}

void MongoSslOpts::SetCrlFile(const char* f) {
    impl_->crl_file = f ? f : "";
    impl_->opts.crl_file = f ? impl_->crl_file.c_str() : nullptr;
}

void MongoSslOpts::SetWeakCertValidation(bool weak) { impl_->opts.weak_cert_validation = weak; }
void MongoSslOpts::SetAllowInvalidHostname(bool allow) { impl_->opts.allow_invalid_hostname = allow; }

const char* MongoSslOpts::GetPemFile() const { return impl_->opts.pem_file; }
const char* MongoSslOpts::GetPemPwd() const { return impl_->opts.pem_pwd; }
const char* MongoSslOpts::GetCaFile() const { return impl_->opts.ca_file; }
const char* MongoSslOpts::GetCaDir() const { return impl_->opts.ca_dir; }
const char* MongoSslOpts::GetCrlFile() const { return impl_->opts.crl_file; }
bool MongoSslOpts::GetWeakCertValidation() const { return impl_->opts.weak_cert_validation; }
bool MongoSslOpts::GetAllowInvalidHostname() const { return impl_->opts.allow_invalid_hostname; }

const void* MongoSslOpts::GetDefault() { return mongoc_ssl_opt_get_default(); }
void* MongoSslOpts::Raw() { return &impl_->opts; }

} // namespace mongo
} // namespace engine
