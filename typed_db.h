#pragma once

extern "C" {
#include <stdint.h>
#include <string.h>
}

#include "assert.h"
#include "optional.h"

#include <leveldb/db.h>
#include <leveldb/options.h>

struct Hash {
    char hash[64];
};

template <typename T>
static void calc_hash(const T *buf, Hash *out_hash) {
    static_assert(std::is_pod<T>::value, "Value must be is_pod");
    // TODO
    bzero(out_hash->hash, sizeof(out_hash->hash));
    for (uint32_t i = 0; i < sizeof(T); i++) {
        out_hash->hash[i % sizeof(out_hash->hash)] += ((char*)buf)[i];
    }
}

static inline leveldb::Slice hash_to_slice(const Hash *hash)
{
    return leveldb::Slice(hash->hash, sizeof(hash));
}

template <typename V>
class Key {
public:
    virtual const Hash *get_hash() const = 0;
};

class TypedDB {
private:
    leveldb::DB *db;
public:

    TypedDB() {
        leveldb::Options options;
        options.create_if_missing = true;
        const char *db_name = "commands.db";
        DEBUG("Opening db: " << db_name);
        auto status = leveldb::DB::Open(options, db_name, &this->db);
        ASSERT(status.ok()); //, status.ToString());
    }

    ~TypedDB() {
        DEBUG("Closing db");
        delete this->db;
    }

    template <typename V> Optional<V> TryGet(const Key<V> &key) const {
        leveldb::ReadOptions options;
        std::string str;
        leveldb::Slice slice = hash_to_slice(key.get_hash());
        auto status = this->db->Get(options, slice, &str);
        if (status.ok()) {
            ASSERT(str.size() == sizeof(V)); //, "invalid size");
            return Optional<V>(str.data(), str.size());
        }
        if (status.IsNotFound()) {
            return Optional<V>();
        }
        PANIC("got error:"); // << status.ToString());
    }

    template <typename V> void Put(const Key<V> &key, const V *value) {
        leveldb::Slice slice = hash_to_slice(key.get_hash());
        leveldb::WriteOptions write_options;
        this->db->Put(write_options, slice, leveldb::Slice((char*)value, sizeof(*value)));
    }
};

#include "typed_db_private.h"
