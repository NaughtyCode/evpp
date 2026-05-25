#include "runtime/database/mongo/mongo_host_list.h"

#include <mongoc/mongoc.h>

#include <cstring>
#include <mutex>

namespace engine {
namespace mongo {

struct MongoHostList::Impl {
    mongoc_host_list_t entry;
    MongoHostList* next = nullptr;
    mutable std::once_flag next_once;
};

MongoHostList::MongoHostList() : impl_(std::make_unique<Impl>()) {
    memset(&impl_->entry, 0, sizeof(impl_->entry));
}

MongoHostList::~MongoHostList() {
    delete impl_->next;
}

MongoHostList::MongoHostList(MongoHostList&&) noexcept = default;
MongoHostList& MongoHostList::operator=(MongoHostList&& other) noexcept {
    if (this != &other) {
        delete impl_->next;
        impl_->next = nullptr;
        impl_ = std::move(other.impl_);
    }
    return *this;
}

const char* MongoHostList::GetHost() const { return impl_->entry.host; }
const char* MongoHostList::GetHostAndPort() const { return impl_->entry.host_and_port; }
uint16_t MongoHostList::GetPort() const { return impl_->entry.port; }
int MongoHostList::GetFamily() const { return impl_->entry.family; }

MongoHostList* MongoHostList::GetNext() const {
    if (!impl_->entry.next) return nullptr;
    std::call_once(impl_->next_once, [this]() {
        auto* self = const_cast<MongoHostList*>(this);
        self->impl_->next = new MongoHostList();
        memcpy(&self->impl_->next->impl_->entry, impl_->entry.next, sizeof(mongoc_host_list_t));
    });
    return impl_->next;
}

void* MongoHostList::Raw() { return &impl_->entry; }

} // namespace mongo
} // namespace engine
