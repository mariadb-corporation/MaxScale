/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

//
// REDIS implementation of the storage API of the MaxScale cache filter
//
// Documentation:
// - /usr/include/hiredis
// - https://github.com/redis/hiredis/blob/master/README.md
// - https://redis.io/commands
//

//
// Without invalidation, all that is needed are operations for GETTING, PUTTING
// and DELETING a value corresponding to a particular key. Those operations
// correspond to the Redis commands GET, SET and DEL, respectively.
//
// With invalidation, things get more complicated as when a table is modified,
// we need to know which keys should be deleted. Fortunately, Redis has support
// for hash-tables, using which that can be handled. So, in principle storing a
// value to Redis is handled as follows.
//
// Assume the following statement: "SELECT * FROM tbl". The key - key1 - is
// created from the entire statement, the value - val1 - is the result set from
// the server, and the invalidation words are "tbl".
//
// Storing
//     SET key1 val1
//     HSET tbl key1 "1"
//
// The SET command simply stores the value val1 at the key key1.
// The HSET command stores the field key1 with the value "1" to the hash
// named "tbl".
//
// Fetching
//     GET key1
//
// Deleting
//     DEL key1
//
// Note that we do not modify the hash; deleting will not be performed other
// than in error situations (and at the time of this writing is considered to
// be removed entirelly) and it does not really matter if an non-existing key
// is in the hash.
//
// Invalidating
//     HGETALL tbl
//     DEL key1 key2 key3 ...
//     HDEL tbl key1 key2 key3 ...
//
// The keys are the ones returned by HGETALL. So, at invalidation time we fetch
// all keys dependent on the invalidation word (aka table name), then delete
// the keys themselves and the keys from the hash.
//
// The problem here is that between HGETALL and (DEL + HDEL) some other session
// may store a new field to the 'tbl' hash, and a value that should be deleted
// at this point. Now it won't be deleted until the next time that same
// invalidation is performed.
//
// For correctness, the (SET + HSET) of the storing and the (HGETALL + DEL + HDEL)
// of the invalidation must be performed as transactions.
//
// Redis does not have a concept of transactions that could be used for this
// purpose but it does have the means for doing things optimistically so that
// concurrent updates are detected.
//
// Storing
//     WATCH tbl:lock
//     MULTI
//     MSET tbl:lock "1"
//     SET key1 val1
//     HSET tbl key1 "1"
//     EXEC
//
// With WATCH (one request-response) we tell Redis that the key tbl:lock (a key
// that does not have to exist) should be watched. Then with MULTI we collect
// the commands that should be executed within one request-response. For obvious
// reasons, no command within MULTI may depend upon the result of an earlier
// command as we will not see those before the EXEC, when the actual execution
// will then take place.
//
// The above requires 2 round-trips; one for the WATCH and one for the MULTI.
//
// Now, since we modified the watched key - tbl:lock - within MULTI, if somebody
// else modifies the same watched key, the entire MULTI block will fail.
//
// Invalidation
//     WATCH tbl:lock
//     HGETALL tbl
//     MULTI
//     MSET tbl:lock "1"
//     DEL key1 key2 key3 ...
//     HDEL tbl key1 key2 key3 ...
//     EXEC
//
// So, first we start watching the variable, then we get all keys of the hash
// and finally within a MULTI block update the watch variable, delete the keys
// and the keys in the hash.
//
// The above requires 3 round-trips; one for the WATCH, one for the HGETALL and
// one for the MULTI.
//
// When something fails due to a conflict, all you need to do is to redo the
// whole thing.
//
// This arrangement ensures that the storing and invalidation of items that
// are interdependent cannot happen in a way that could cause actions to be
// lost.
//
// However, it appears that it is possible to enter a live lock; everyone
// encounters a conflict over and over again. To prevent that the number of
// redo times must be limited. From a correctness point of view, if the
// storing fails, it is sufficient to turn off the caching for the session in
// question, but if the invalidation fails, then caching should be disabled
// for everyone.
//

#define MXS_MODULE_NAME "storage_redis"
#include "redisstorage.hh"
#include <hiredis/hiredis.h>
#include <maxscale/threadpool.hh>

using std::shared_ptr;
using std::string;
using std::vector;

namespace
{

const char* redis_type_to_string(int type)
{
    switch (type)
    {
    case REDIS_REPLY_ARRAY:
        return "ARRAY";

    case REDIS_REPLY_ERROR:
        return "ERROR";

    case REDIS_REPLY_INTEGER:
        return "INTEGER";

    case REDIS_REPLY_NIL:
        return "NIL";

    case REDIS_REPLY_STATUS:
        return "STATUS";

    case REDIS_REPLY_STRING:
        return "STRING";
    }

    return "UNKNOWN";
}

string redis_error_to_string(int err)
{
    switch (err)
    {
    case REDIS_OK:
        return "no error";

    case REDIS_ERR_IO:
        {
            int e = errno;
            string s("redis I/O error: ");
            s += mxb_strerror(e);
        }
        break;

    case REDIS_ERR_EOF:
        return "server closed the connection";

    case REDIS_ERR_PROTOCOL:
        return "error while parsing the protocol";

    case REDIS_ERR_OTHER:
        return "unspecified error (possibly unresolved hostname)";

    case REDIS_ERR:
        return "general error";
    }

    return "unknown error";
}

class Redis
{
public:
    class Reply
    {
    public:
        enum Ownership
        {
            OWNED,
            BORROWED,
        };

        Reply(const Reply&) = delete;
        Reply& operator=(const Reply&) = delete;

        Reply()
            : m_pReply(nullptr)
            , m_ownership(OWNED)
        {
        }

        Reply(redisReply* pReply, Ownership ownership = OWNED)
            : m_pReply(pReply)
            , m_ownership(ownership)
        {
        }

        Reply(Reply&& other)
            : m_pReply(other.m_pReply)
            , m_ownership(other.m_ownership)
        {
            other.m_pReply = nullptr;
            other.m_ownership = OWNED;
        }

        Reply& operator = (Reply&& rhs)
        {
            reset(rhs.m_pReply, rhs.m_ownership);

            rhs.m_pReply = nullptr;
            rhs.m_ownership = OWNED;

            return *this;
        }

        ~Reply()
        {
            reset();
        }

        explicit operator bool () const
        {
            return m_pReply != nullptr;
        }

        void reset()
        {
            reset(nullptr);
        }

        void reset(redisReply* pReply, Ownership ownership = OWNED)
        {
            if (m_pReply && m_ownership == OWNED)
            {
                freeReplyObject(m_pReply);
            }

            m_pReply = pReply;
            m_ownership = ownership;
        }

        int type() const
        {
            mxb_assert(m_pReply);
            return m_pReply->type;
        }

        bool is_array() const
        {
            mxb_assert(m_pReply);
            return m_pReply->type == REDIS_REPLY_ARRAY;
        }

        bool is_error() const
        {
            mxb_assert(m_pReply);
            return m_pReply->type == REDIS_REPLY_ERROR;
        }

        bool is_integer() const
        {
            mxb_assert(m_pReply);
            return m_pReply->type == REDIS_REPLY_INTEGER;
        }

        bool is_nil() const
        {
            mxb_assert(m_pReply);
            return m_pReply->type == REDIS_REPLY_NIL;
        }

        bool is_status(const char* zValue = nullptr) const
        {
            mxb_assert(m_pReply);

            bool rv = m_pReply->type == REDIS_REPLY_STATUS;

            if (rv && zValue)
            {
                rv = strcmp(m_pReply->str, zValue) == 0;
            }

            return rv;
        }

        bool is_string() const
        {
            mxb_assert(m_pReply);
            return m_pReply->type == REDIS_REPLY_STRING;
        }

        long long integer() const
        {
            mxb_assert(is_integer());
            return m_pReply->integer;
        }

        const char* str() const
        {
            mxb_assert(is_error() || is_status() || is_string());
            return m_pReply->str;
        }

        size_t len() const
        {
            mxb_assert(is_error() || is_status() || is_string());
            return m_pReply->len;
        }

        size_t elements() const
        {
            mxb_assert(is_array());
            return m_pReply->elements;
        }

        Reply element(size_t i) const
        {
            mxb_assert(is_array());
            mxb_assert(i < m_pReply->elements);

            return Reply(m_pReply->element[i], Reply::BORROWED);
        }

    private:
        redisReply* m_pReply;
        Ownership   m_ownership;
    };

    Redis(const Redis&) = delete;
    Redis& operator=(const Redis&) = delete;

    Redis(redisContext* pContext)
        : m_pContext(pContext)
    {
    }

    const char* errstr() const
    {
        return m_pContext->errstr;
    }

    ~Redis()
    {
        redisFree(m_pContext);
    }

    Reply command(const char* zFormat, ...)
    {
        va_list ap;
        va_start(ap, zFormat);
        void *reply = redisvCommand(m_pContext, zFormat, ap);
        va_end(ap);

        return Reply(static_cast<redisReply*>(reply));
    }

    Reply command(int argc, const char **argv, const size_t *argvlen)
    {
        void* pReply = redisCommandArgv(m_pContext, argc, argv, argvlen);

        return Reply(static_cast<redisReply*>(pReply));
    }

    int appendCommand(const char* zFormat, ...)
    {
        va_list ap;
        int rv;

        va_start(ap, zFormat);
        rv = redisvAppendCommand(m_pContext, zFormat,ap);
        va_end(ap);

        return rv;
    }

    int appendCommandArgv(int argc, const char **argv, const size_t *argvlen)
    {
        return redisAppendCommandArgv(m_pContext, argc, argv, argvlen);
    }

    int getReply(Reply* pReply)
    {
        void* pV;

        int rv = redisGetReply(m_pContext, &pV);

        if (rv == REDIS_OK)
        {
            pReply->reset(static_cast<redisReply*>(pV));
        }

        return rv;
    }

    bool expect_status(const char* zValue, const char* zContext)
    {
        if (!zContext)
        {
            zContext = "unspecified";
        }

        Reply reply;
        int rv = getReply(&reply);

        if (rv == REDIS_OK)
        {
            if (reply.is_status())
            {
                if (strcmp(reply.str(), zValue) != 0)
                {
                    MXS_ERROR("Expected status message '%s' in the context of %s, "
                              "but received '%s'.", zValue, zContext, reply.str());
                    rv = REDIS_ERR;
                }
            }
            else
            {
                MXS_ERROR("Expected status message in the context of %s, "
                          "but received a %s.", zContext, redis_type_to_string(reply.type()));
            }
        }
        else
        {
            MXS_ERROR("Failed to read reply in the context of %s: %s, %s",
                      zContext, redis_error_to_string(rv).c_str(), errstr());
        }

        return rv == REDIS_OK;
    }

    bool expect_n_status(size_t n, const char* zValue, const char* zContext)
    {
        bool rv = true;

        for (size_t i = 0; i < n; ++i)
        {
            if (!expect_status(zValue, zContext))
            {
                rv = false;
            }
        }

        return rv;
    }

private:
    redisContext* m_pContext;
};

class RedisToken : public std::enable_shared_from_this<RedisToken>,
                   public Storage::Token
{
public:
    ~RedisToken()
    {
    }

    shared_ptr<RedisToken> get_shared()
    {
        return shared_from_this();
    }

    static bool create(const string& host,
                       int port,
                       bool invalidate,
                       uint32_t ttl,
                       shared_ptr<Storage::Token>* psToken)
    {
        bool rv = false;
        redisContext* pRedis = redisConnect(host.c_str(), port);

        if (pRedis)
        {
            RedisToken* pToken = new (std::nothrow) RedisToken(pRedis, invalidate, ttl);

            if (pToken)
            {
                psToken->reset(pToken);
                rv = true;
            }
            else
            {
                redisFree(pRedis);
            }
        }
        else
        {
            MXS_ERROR("Could not create redis handle, are the arguments '%s:%d' valid?",
                      host.c_str(), port);
        }

        return rv;
    }

    cache_result_t get_value(const CacheKey& key,
                             uint32_t flags,
                             uint32_t soft_ttl,
                             uint32_t hard_ttl,
                             GWBUF** ppValue,
                             std::function<void (cache_result_t, GWBUF*)> cb)
    {
        vector<char> rkey = key.to_vector();

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, rkey, cb] () {
                Redis::Reply reply = sThis->m_redis.command("GET %b", rkey.data(), rkey.size());

                GWBUF* pValue = nullptr;
                cache_result_t rv = CACHE_RESULT_ERROR;

                if (reply)
                {
                    switch (reply.type())
                    {
                    case REDIS_REPLY_STRING:
                        pValue = gwbuf_alloc_and_load(reply.len(), reply.str());
                        rv = CACHE_RESULT_OK;
                        break;

                    case REDIS_REPLY_NIL:
                        rv = CACHE_RESULT_NOT_FOUND;
                        break;

                    default:
                        MXS_WARNING("Unexpected redis redis return type (%s) received.",
                                    redis_type_to_string(reply.type()));
                    }
                }
                else
                {
                    MXS_WARNING("Fatally failed when fetching cached value from redis: %s",
                                sThis->m_redis.errstr());
                }

                sThis->m_pWorker->execute([sThis, rv, pValue, cb]() {
                        if (sThis.use_count() > 1) // The session is still alive
                        {
                            cb(rv, pValue);
                        }
                        else
                        {
                            gwbuf_free(pValue);
                        }
                    }, mxb::Worker::EXECUTE_QUEUED);
            });

        return CACHE_RESULT_PENDING;
    }

    cache_result_t put_value(const CacheKey& key,
                             const vector<std::string>& invalidation_words,
                             const GWBUF* pValue,
                             std::function<void (cache_result_t)> cb)
    {
        mxb_assert(m_invalidate || invalidation_words.empty());
        vector<char> rkey = key.to_vector();

        GWBUF* pClone = gwbuf_clone(const_cast<GWBUF*>(pValue));
        MXS_ABORT_IF_NULL(pClone);

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, rkey, invalidation_words, pClone, cb]() {
                RedisAction action;

                do
                {
                    action = sThis->put_value(rkey, invalidation_words, pClone);

                    if (action == RedisAction::REDO)
                    {
                        MXS_WARNING("Failed to store value to redis due to conflict when "
                                    "book-keeping dependency to %s, retrying.",
                                    mxb::join(invalidation_words).c_str());
                    }
                }
                while (action == RedisAction::REDO);

                cache_result_t rv = (action == RedisAction::ERROR ? CACHE_RESULT_ERROR : CACHE_RESULT_OK);

                sThis->m_pWorker->execute([sThis, pClone, rv, cb]() {
                        // TODO: So as not to trigger an assert in buffer.cc, we need to delete
                        // TODO: the gwbuf in the same worker where it was allocated. This means
                        // TODO: that potentially a very large buffer is kept around for longer
                        // TODO: than necessary. Perhaps time to stop tracking buffer ownership.
                        gwbuf_free(pClone);

                        if (sThis.use_count() > 1) // The session is still alive
                        {
                            cb(rv);
                        }
                    }, mxb::Worker::EXECUTE_QUEUED);
            });

        return CACHE_RESULT_PENDING;
    }

    cache_result_t del_value(const CacheKey& key,
                             std::function<void (cache_result_t)> cb)
    {
        vector<char> rkey = key.to_vector();

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, rkey, cb] () {
                Redis::Reply reply = sThis->m_redis.command("DEL %b", rkey.data(), rkey.size());

                cache_result_t rv = CACHE_RESULT_ERROR;

                if (reply)
                {
                    if (reply.is_integer())
                    {
                        switch (reply.integer())
                        {
                        case 0:
                            rv = CACHE_RESULT_NOT_FOUND;
                            break;

                        default:
                            MXS_WARNING("Unexpected number of values - %lld - deleted with one key,",
                                        reply.integer());
                            /* FLOWTHROUGH */
                        case 1:
                            rv = CACHE_RESULT_OK;
                            break;
                        }
                    }
                    else
                    {
                        MXS_WARNING("Unexpected redis return type (%s) received.",
                                    redis_type_to_string(reply.type()));
                    }
                }
                else
                {
                    MXS_WARNING("Failed fatally when deleting cached value from redis: %s",
                                sThis->m_redis.errstr());
                }

                sThis->m_pWorker->execute([sThis, rv, cb]() {
                        if (sThis.use_count() > 1) // The session is still alive
                        {
                            cb(rv);
                        }
                    }, mxb::Worker::EXECUTE_QUEUED);
            });

        return CACHE_RESULT_PENDING;
    }

    cache_result_t invalidate(const vector<string>& words,
                              std::function<void (cache_result_t)> cb)
    {
        mxb_assert(m_invalidate);

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, words, cb] () {
                RedisAction action;

                do
                {
                    action = sThis->invalidate(words);

                    if (action == RedisAction::REDO)
                    {
                        MXS_WARNING("Failed to invalidate values dependent on %s, retrying.",
                                    mxb::join(words).c_str());
                    }
                }
                while (action == RedisAction::REDO);

                cache_result_t rv = (action == RedisAction::ERROR ? CACHE_RESULT_ERROR : CACHE_RESULT_OK);

                sThis->m_pWorker->execute([sThis, rv, cb]() {
                        if (sThis.use_count() > 1) // The session is still alive
                        {
                            cb(rv);
                        }
                    }, mxb::Worker::EXECUTE_QUEUED);
            });

        return CACHE_RESULT_PENDING;
    }

private:
    enum class RedisAction
    {
        OK,
        REDO,
        ERROR
    };

    bool watch(const vector<string>& words)
    {
        vector<string> watch_words;
        vector<const char*> argv;
        vector<size_t> argvlen;

        argv.push_back("WATCH");
        argvlen.push_back(5);

        for (const auto& word : words)
        {
            watch_words.emplace_back(word + ":lock");

            argv.push_back(watch_words.back().c_str());
            argvlen.push_back(watch_words.back().length());
        }

        Redis::Reply reply = m_redis.command(argv.size(), argv.data(), argvlen.data());

        return reply.is_status("OK");
    }

    void touch(const vector<string>& words)
    {
        vector<string> watch_words;
        vector<const char*> argv;
        vector<size_t> argvlen;

        argv.push_back("MSET");
        argvlen.push_back(4);

        for (const auto& word : words)
        {
            watch_words.emplace_back(word + ":lock");

            argv.push_back(watch_words.back().c_str());
            argvlen.push_back(watch_words.back().length());

            argv.push_back("\"\"");
            argvlen.push_back(2);
        }

        MXB_AT_DEBUG(int rc =) m_redis.appendCommandArgv(argv.size(), argv.data(), argvlen.data());
        mxb_assert(rc == REDIS_OK);
    }

    RedisAction put_value(const vector<char>& rkey,
                          const vector<std::string>& invalidation_words,
                          GWBUF* pClone)
    {
        RedisAction action = RedisAction::OK;

        size_t n = invalidation_words.size();

        if (n != 0)
        {
            if (!watch(invalidation_words))
            {
                return RedisAction::ERROR;
            }
        }

        int rc;
        // Start a redis transaction.
        MXB_AT_DEBUG(rc =) m_redis.appendCommand("MULTI");
        mxb_assert(rc == REDIS_OK);

        if (n != 0)
        {
            touch(invalidation_words);

            // 'rkey' is the key that identifies the value. So, we store it to
            // a redis hash that is identified by each invalidation word, aka
            // the table name.

            for (size_t i = 0; i < n; ++i)
            {
                const char* pHash = invalidation_words[i].c_str();
                int hash_len = invalidation_words[i].length();
                const char* pField = rkey.data();
                int field_len = rkey.size();

                // redisAppendCommand can only fail if we run out of memory
                // or if the format string is broken.
                MXB_AT_DEBUG(rc =) m_redis.appendCommand("HSET %b %b \"1\"",
                                                         pHash, hash_len,
                                                         pField, field_len);
                mxb_assert(rc == REDIS_OK);
            }
        }

        // Then the actual value is stored.
        MXB_AT_DEBUG(rc =) m_redis.appendCommand("SET %b %b%s",
                                                 rkey.data(), rkey.size(),
                                                 reinterpret_cast<const char*>(GWBUF_DATA(pClone)),
                                                 GWBUF_LENGTH(pClone),
                                                 m_set_options.c_str());
        mxb_assert(rc == REDIS_OK);

        // Commit the transaction, will actually be sent only when we ask for the reply.
        MXB_AT_DEBUG(rc =) m_redis.appendCommand("EXEC");
        mxb_assert(rc == REDIS_OK);

        // This will be the response to MULTI above.
        if (m_redis.expect_status("OK", "MULTI"))
        {
            // All commands before EXEC should only return a status of QUEUED.
            m_redis.expect_n_status(2 * n + 1, "QUEUED", "queued command");

            // The reply to EXEC
            Redis::Reply reply;
            rc = m_redis.getReply(&reply);

            if (rc == REDIS_OK)
            {
                if (reply.is_nil())
                {
                    action = RedisAction::REDO;
                }
                else
                {
                    // The reply will now contain the actual responses to the commands
                    // issued after MULTI.
                    mxb_assert(reply.is_array());
                    mxb_assert(reply.elements() == 2 * n + 1);

                    Redis::Reply element;

#ifdef SS_DEBUG
                    size_t i;
                    // First we handle the replies to the touch "MSET" commands.
                    for (i = 0; i < n; ++i)
                    {
                        element = reply.element(i);
                        mxb_assert(element.is_status("OK"));
                    }

                    // Then we handle the replies to the "HSET" commands.
                    for (;i < 2 * n; ++i)
                    {
                        element = reply.element(i);
                        mxb_assert(element.is_integer());
                    }
#endif

                    // Then the SET
                    element = reply.element(2 * n);
                    mxb_assert(element.is_status());

                    if (!element.is_status("OK"))
                    {
                        MXS_ERROR("Failed when storing cache value to redis, expected 'OK' but "
                                  "received '%s'.", reply.str());
                        action = RedisAction::ERROR;
                    }
                }
            }
            else
            {
                MXS_WARNING("Failed fatally when reading reply to EXEC: %s, %s",
                            redis_error_to_string(rc).c_str(),
                            m_redis.errstr());
                action = RedisAction::ERROR;
            }
        }
        else
        {
            MXS_ERROR("Failed when reading response to MULTI: %s, %s",
                      redis_error_to_string(rc).c_str(),
                      m_redis.errstr());
            action = RedisAction::ERROR;
        }

        return action;
    }

    RedisAction invalidate(const vector<string>& words)
    {
        RedisAction action = RedisAction::OK;

        size_t n = words.size();

        if (n != 0)
        {
            if (!watch(words))
            {
                return RedisAction::ERROR;
            }
        }

        int rc;
        if (n != 0)
        {
            // For each invalidation word (aka table name) we fetch all
            // keys.
            for (size_t i = 0; i < n; ++i)
            {
                const char* pHash = words[i].c_str();
                int hash_len = words[i].length();

                // redisAppendCommand can only fail if we run out of memory
                // or if the format string is broken.
                MXB_AT_DEBUG(rc =) m_redis.appendCommand("HGETALL %b",
                                                         pHash, hash_len);
                mxb_assert(rc == REDIS_OK);
            }
        }

        // Then we iterate over the replies and build one DEL command for
        // deleting all values and one HDEL for each invalidation word for
        // deleting the keys of each word.

        vector<Redis::Reply> to_free;

        vector<vector<const char*>> hdel_argvs;
        vector<vector<size_t>> hdel_argvlens;

        vector<const char*> del_argv;
        vector<size_t> del_argvlen;

        del_argv.push_back("DEL");
        del_argvlen.push_back(3);

        for (size_t i = 0; i < n; ++i)
        {
            Redis::Reply reply;
            rc = m_redis.getReply(&reply);

            if (rc == REDIS_OK)
            {
                mxb_assert(reply.is_array());

                if (reply.is_array())
                {
                    vector<const char*> hdel_argv;
                    vector<size_t> hdel_argvlen;

                    hdel_argv.push_back("HDEL");
                    hdel_argvlen.push_back(4);

                    hdel_argv.push_back(words[i].c_str());
                    hdel_argvlen.push_back(words[i].length());

                    // 'j = j + 2' since key/value pairs are returned.
                    for (size_t j = 0; j < reply.elements(); j = j + 2)
                    {
                        Redis::Reply element = reply.element(j);

                        if (element.is_string())
                        {
                            del_argv.push_back(element.str());
                            del_argvlen.push_back(element.len());

                            hdel_argv.push_back(element.str());
                            hdel_argvlen.push_back(element.len());
                        }
                        else
                        {
                            MXS_ERROR("Unexpected type returned by redis: %s",
                                      redis_type_to_string(element.type()));
                        }
                    }

                    hdel_argvs.push_back(std::move(hdel_argv));
                    hdel_argvlens.push_back(std::move(hdel_argvlen));
                }

                to_free.emplace_back(std::move(reply));
            }
            else
            {
                MXS_ERROR("Could not read redis reply for hash update for '%s': %s, %s",
                          words[i].c_str(),
                          redis_error_to_string(rc).c_str(),
                          m_redis.errstr());
            }
        }

        if (del_argv.size() > 1)
        {
            rc = m_redis.appendCommand("MULTI");
            mxb_assert(rc == REDIS_OK);

            touch(words);

            // Delete the relevant keys from the hashes.
            for (size_t i = 0; i < hdel_argvs.size(); ++i)
            {
                // Delete keys related to a particular table, the HDEL commands.
                const vector<const char*>& hdel_argv = hdel_argvs[i];
                const vector<size_t>& hdel_argvlen = hdel_argvlens[i];

                if (hdel_argv.size() > 2)
                {
                    const char** ppHdel_argv = const_cast<const char**>(hdel_argv.data());
                    MXB_AT_DEBUG(rc =) m_redis.appendCommandArgv(hdel_argv.size(),
                                                                 ppHdel_argv,
                                                                 hdel_argvlen.data());
                    mxb_assert(rc == REDIS_OK);
                }
            }

            // Delete all values, the DEL command.
            const char** ppDel_argv = const_cast<const char**>(del_argv.data());
            rc = m_redis.appendCommandArgv(del_argv.size(),
                                           ppDel_argv,
                                           del_argvlen.data());
            mxb_assert(rc == REDIS_OK);

            // This will actually send everything.
            rc = m_redis.appendCommand("EXEC");
            mxb_assert(rc == REDIS_OK);

            // This will be the response to MULTI above.
            if (m_redis.expect_status("OK", "MULTI"))
            {
                // All commands before EXEC should only return a status of QUEUED.
                m_redis.expect_n_status(words.size() + hdel_argvs.size() + 1, "QUEUED", "queued command");

                // The reply to EXEC
                Redis::Reply reply;
                rc = m_redis.getReply(&reply);

                if (rc == REDIS_OK)
                {
                    if (reply.is_nil())
                    {
                        action = RedisAction::REDO;
                    }
                    else
                    {
                        // The reply will not contain the actual responses to the commands
                        // issued after MULTI.
                        mxb_assert(reply.is_array());
                        mxb_assert(reply.elements() == words.size() + hdel_argvs.size() + 1);

#ifdef SS_DEBUG
                        Redis::Reply element;
                        size_t i;
                        // First we handle the replies to the touch "MSET" commands.
                        for (i = 0; i < words.size(); ++i)
                        {
                            element = reply.element(i);
                            mxb_assert(element.is_status("OK"));
                        }

                        // Then we handle the replies to the "HDEL" commands.
                        for (; i < words.size() + hdel_argvs.size(); ++i)
                        {
                            element = reply.element(i);
                            mxb_assert(element.is_integer());
                        }

                        // Finally the DEL itself.
                        element = reply.element(words.size() + hdel_argvs.size());
                        mxb_assert(element.is_integer());
#endif
                    }
                }
                else
                {
                    MXS_ERROR("Could not read EXEC reply from redis, the cache is now "
                              "in an unknown state: %s, %s",
                              redis_error_to_string(rc).c_str(),
                              m_redis.errstr());
                    action = RedisAction::ERROR;
                }
            }
            else
            {
                MXS_ERROR("Could not read MULTI reply from redis, the cache is now "
                          "in an unknown state: %s, %s",
                          redis_error_to_string(rc).c_str(),
                          m_redis.errstr());
                action = RedisAction::ERROR;
            }
        }

        // Does this work? Probably not in all cases; it appears that WATCH
        // needs to be used to prevent problems caused by the fetching of the keys
        // and the deleteing of the keys (and values) being done in separate
        // transactions.

        return action;
    }

    RedisToken(redisContext* pRedis, bool invalidate, uint32_t ttl)
        : m_redis(pRedis)
        , m_pWorker(mxb::Worker::get_current())
        , m_invalidate(invalidate)
    {
        if (ttl != 0)
        {
            m_set_options += " PX "; // The leading space is significant.
            m_set_options += std::to_string(ttl);
        }
    }

private:
    Redis        m_redis;
    mxb::Worker* m_pWorker;
    bool         m_invalidate;
    std::string  m_set_options;
};

}


RedisStorage::RedisStorage(const string& name,
                           const Config& config,
                           const string& host,
                           int port)
    : m_name(name)
    , m_config(config)
    , m_host(host)
    , m_port(port)
    , m_invalidate(config.invalidate != CACHE_INVALIDATE_NEVER)
    , m_ttl(config.hard_ttl)
{
    if (config.soft_ttl != config.hard_ttl)
    {
        MXS_WARNING("The storage storage_redis does not distinguish between "
                    "soft (%u ms) and hard ttl (%u ms). Hard ttl is used.",
                    config.soft_ttl, config.hard_ttl);
    }
}

RedisStorage::~RedisStorage()
{
}

//static
bool RedisStorage::initialize(cache_storage_kind_t* pKind, uint32_t* pCapabilities)
{
    *pKind = CACHE_STORAGE_SHARED;
    *pCapabilities = (CACHE_STORAGE_CAP_ST | CACHE_STORAGE_CAP_MT | CACHE_STORAGE_CAP_INVALIDATION);
    return true;
}

//static
void RedisStorage::finalize()
{
}

//static
RedisStorage* RedisStorage::create(const string& name,
                                   const Config& config,
                                   const std::string& arguments)
{
    RedisStorage* pStorage = nullptr;

    if (config.max_size != 0)
    {
        MXS_WARNING("The storage storage_redis does not support specifying "
                    "a maximum size of the cache storage.");
    }

    if (config.max_count != 0)
    {
        MXS_WARNING("The storage storage_redis does not support specifying "
                    "a maximum number of items in the cache storage.");
    }

    vector<string> vs = mxb::strtok(arguments, ":");

    if (vs.size() == 2)
    {
        string host = vs[0];
        int port;

        if (mxb::get_int(vs[1], &port) && port > 0)
        {
            pStorage = new (std::nothrow) RedisStorage(name, config, host, port);
        }
        else
        {
            MXS_ERROR("The provided arugments '%s' does not translate into a valid "
                      "host:port combination.", arguments.c_str());
        }
    }
    else
    {
        MXS_ERROR("storage_redis expects a `storage_options` argument of "
                  "HOST:PORT format: %s", arguments.c_str());
    }

    return pStorage;
}

bool RedisStorage::create_token(shared_ptr<Storage::Token>* psToken)
{
    return RedisToken::create(m_host, m_port, m_invalidate, m_ttl, psToken);
}

void RedisStorage::get_config(Config* pConfig)
{
    *pConfig = m_config;
}

cache_result_t RedisStorage::get_info(uint32_t what, json_t** ppInfo) const
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::get_value(Storage::Token* pToken,
                                       const CacheKey& key,
                                       uint32_t flags,
                                       uint32_t soft_ttl,
                                       uint32_t hard_ttl,
                                       GWBUF** ppValue,
                                       std::function<void (cache_result_t, GWBUF*)> cb)
{
    mxb_assert(pToken);

    return static_cast<RedisToken*>(pToken)->get_value(key, flags, soft_ttl, hard_ttl, ppValue, cb);
}

cache_result_t RedisStorage::put_value(Token* pToken,
                                       const CacheKey& key,
                                       const vector<string>& invalidation_words,
                                       const GWBUF* pValue,
                                       std::function<void (cache_result_t)> cb)
{
    mxb_assert(pToken);

    return static_cast<RedisToken*>(pToken)->put_value(key, invalidation_words, pValue, cb);
}

cache_result_t RedisStorage::del_value(Token* pToken,
                                       const CacheKey& key,
                                       std::function<void (cache_result_t)> cb)
{
    mxb_assert(pToken);

    return static_cast<RedisToken*>(pToken)->del_value(key, cb);
}

cache_result_t RedisStorage::invalidate(Token* pToken,
                                        const vector<string>& words,
                                        std::function<void (cache_result_t)> cb)
{
    mxb_assert(pToken);

    return static_cast<RedisToken*>(pToken)->invalidate(words, cb);
}

cache_result_t RedisStorage::clear(Token* pToken)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::get_head(CacheKey* pKey, GWBUF** ppHead)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::get_tail(CacheKey* pKey, GWBUF** ppHead)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::get_size(uint64_t* pSize) const
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::get_items(uint64_t* pItems) const
{
    return CACHE_RESULT_ERROR;
}
