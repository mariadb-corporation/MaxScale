/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
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
// for sets using which that can be handled. So, in principle storing a
// value to Redis is handled as follows.
//
// Assume the following statement: "SELECT * FROM tbl". The key - key1 - is
// created from the entire statement, the value - val1 - is the result set from
// the server, and the invalidation words are "tbl".
//
// Storing
//     SET key1 val1
//     SADD tbl key1
//
// The SET command simply stores the value val1 at the key key1.
// The SADD command adds the member key1 to the set named "tbl".
//
// Fetching
//     GET key1
//
// Deleting
//     DEL key1
//
// Note that we do not modify the set; deleting will not be performed other
// than in error situations (and at the time of this writing is considered to
// be removed entirelly) and it does not really matter if an non-existing key
// is in the set.
//
// Invalidating
//     SMEMBERS tbl
//     DEL key1 key2 key3 ...
//     SREM tbl key1 key2 key3 ...
//
// The keys are the ones returned by SMEMBERS. So, at invalidation time we fetch
// all keys dependent on the invalidation word (aka table name), then delete
// the keys themselves and the keys from the set.
//
// NOTE: The following was the original approach. However, as that really will
//       only protect against some issues, but not all, it was deemed better not
//       to use WATCH, which not only causes an overhead but forces you to deal
//       with retries that potentially never would succeed. So, invalidation
//       will be only best-effort and the limitations are documented. Drop all
//       lines with WATCH and it is the current approach.
//
// The problem here is that between SMEMBERS and (DEL + SREM) some other session
// may store a new field to the 'tbl' set, and a value that should be deleted
// at this point. Now it won't be deleted until the next time that same
// invalidation is performed.
//
// For correctness, the (SET + SADD) of the storing and the (SMEMBERS + DEL + SREM)
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
//     SADD tbl key1
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
//     SMEMBERS tbl
//     MULTI
//     MSET tbl:lock "1"
//     DEL key1 key2 key3 ...
//     SREM tbl key1 key2 key3 ...
//     EXEC
//
// So, first we start watching the variable, then we get all keys of the set
// and finally within a MULTI block update the watch variable, delete the keys
// and the keys in the set.
//
// The above requires 3 round-trips; one for the WATCH, one for the SMEMBERS and
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
#include <hiredis.h>
#include <maxbase/alloc.h>
#include <maxbase/worker.hh>
#include <maxscale/threadpool.hh>

using std::map;
using std::shared_ptr;
using std::string;
using std::vector;

namespace
{

const int DEFAULT_REDIS_PORT = 6379;

struct
{
    Storage::Limits default_limits;
} this_unit =
{
    Storage::Limits(512 * 1024 * 1024) // max_value_size, https://redis.io/topics/data-types
};

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

    Redis(redisContext* pContext = nullptr)
        : m_pContext(pContext)
    {
    }

    void reset(redisContext* pContext)
    {
        redisFree(m_pContext);
        m_pContext = pContext;
    }

    bool connected() const
    {
        return m_pContext && (m_pContext->flags & REDIS_CONNECTED);
    }

    const char* errstr() const
    {
        mxb_assert(m_pContext);
        return m_pContext->errstr;
    }

    ~Redis()
    {
        redisFree(m_pContext);
    }

    Reply command(const char* zFormat, ...)
    {
        mxb_assert(m_pContext);

        va_list ap;
        va_start(ap, zFormat);
        void *reply = redisvCommand(m_pContext, zFormat, ap);
        va_end(ap);

        return Reply(static_cast<redisReply*>(reply));
    }

    Reply command(int argc, const char **argv, const size_t *argvlen)
    {
        mxb_assert(m_pContext);

        void* pReply = redisCommandArgv(m_pContext, argc, argv, argvlen);

        return Reply(static_cast<redisReply*>(pReply));
    }

    int appendCommand(const char* zFormat, ...)
    {
        mxb_assert(m_pContext);

        va_list ap;
        int rv;

        va_start(ap, zFormat);
        rv = redisvAppendCommand(m_pContext, zFormat,ap);
        va_end(ap);

        return rv;
    }

    int appendCommandArgv(int argc, const char **argv, const size_t *argvlen)
    {
        mxb_assert(m_pContext);

        return redisAppendCommandArgv(m_pContext, argc, argv, argvlen);
    }

    int getReply(Reply* pReply)
    {
        mxb_assert(m_pContext);

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
        mxb_assert(m_pContext);

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
        mxb_assert(m_pContext);

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
                       std::chrono::milliseconds timeout,
                       bool invalidate,
                       uint32_t ttl,
                       shared_ptr<Storage::Token>* psToken)
    {
        bool rv = false;

        RedisToken* pToken = new (std::nothrow) RedisToken(host, port, timeout, invalidate, ttl);

        if (pToken)
        {
            psToken->reset(pToken);

            // The call to connect() (-> get_shared() -> shared_from_this()) can be made only
            // after the pointer has been stored in a shared_ptr.
            pToken->connect();
            rv = true;
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
        if (!connected())
        {
            reconnect();
            return CACHE_RESULT_NOT_FOUND;
        }

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

                    case REDIS_REPLY_ERROR:
                        MXS_ERROR("Redis replied with error: %s", sThis->m_redis.errstr());
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
                             const std::function<void (cache_result_t)>& cb)
    {
        if (!connected())
        {
            reconnect();
            return CACHE_RESULT_OK;
        }

        mxb_assert(m_invalidate || invalidation_words.empty());
        vector<char> rkey = key.to_vector();

        GWBUF* pClone = gwbuf_clone(const_cast<GWBUF*>(pValue));
        MXS_ABORT_IF_NULL(pClone);

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, rkey, invalidation_words, pClone, cb]() {
                RedisAction action = sThis->put_value(rkey, invalidation_words, pClone);

                cache_result_t rv = (action == RedisAction::OK ? CACHE_RESULT_OK : CACHE_RESULT_ERROR);

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
                             const std::function<void (cache_result_t)>& cb)
    {
        if (!connected())
        {
            reconnect();
            return CACHE_RESULT_NOT_FOUND;
        }

        vector<char> rkey = key.to_vector();

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, rkey, cb] () {
                Redis::Reply reply = sThis->m_redis.command("DEL %b", rkey.data(), rkey.size());

                cache_result_t rv = CACHE_RESULT_ERROR;

                if (reply)
                {
                    switch (reply.type())
                    {
                    case REDIS_REPLY_INTEGER:
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
                        break;

                    case REDIS_REPLY_ERROR:
                        MXS_ERROR("Redis replied with error: %s", sThis->m_redis.errstr());
                        break;

                    default:
                        MXS_WARNING("Unexpected redis return type (%s) received.",
                                    redis_type_to_string(reply.type()));
                        break;
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
                              const std::function<void (cache_result_t)>& cb)
    {
        mxb_assert(m_invalidate);

        if (!connected())
        {
            reconnect();
            return CACHE_RESULT_OK;
        }

        auto sThis = get_shared();

        mxs::thread_pool().execute([sThis, words, cb] () {
                RedisAction action = sThis->invalidate(words);

                cache_result_t rv = (action == RedisAction::OK ? CACHE_RESULT_OK : CACHE_RESULT_ERROR);

                sThis->m_pWorker->execute([sThis, rv, cb]() {
                        if (sThis.use_count() > 1) // The session is still alive
                        {
                            cb(rv);
                        }
                    }, mxb::Worker::EXECUTE_QUEUED);
            });

        return CACHE_RESULT_PENDING;
    }

    cache_result_t clear()
    {
        if (!connected())
        {
            reconnect();
            return CACHE_RESULT_OK;
        }

        Redis::Reply reply = m_redis.command("FLUSHALL");

        mxb_assert(reply.is_status("OK"));

        return reply.is_status("OK") ? CACHE_RESULT_OK : CACHE_RESULT_ERROR;
    }

private:
    enum class RedisAction
    {
        OK,
        RETRY,
        ERROR
    };

    RedisAction put_value(const vector<char>& rkey,
                          const vector<std::string>& invalidation_words,
                          GWBUF* pClone)
    {
        RedisAction action = RedisAction::OK;

        int rc;
        // Start a redis transaction.
        MXB_AT_DEBUG(rc =) m_redis.appendCommand("MULTI");
        mxb_assert(rc == REDIS_OK);

        size_t n = invalidation_words.size();
        if (n != 0)
        {
            // 'rkey' is the key that identifies the value. So, we store it to
            // a redis set that is identified by each invalidation word, aka
            // the table name.

            for (size_t i = 0; i < n; ++i)
            {
                const char* pSet = invalidation_words[i].c_str();
                int set_len = invalidation_words[i].length();
                const char* pField = rkey.data();
                int field_len = rkey.size();

                // redisAppendCommand can only fail if we run out of memory
                // or if the format string is broken.
                MXB_AT_DEBUG(rc =) m_redis.appendCommand("SADD %b %b",
                                                         pSet, set_len,
                                                         pField, field_len);
                mxb_assert(rc == REDIS_OK);
            }
        }

        // Then the actual value is stored.
        MXB_AT_DEBUG(rc =) m_redis.appendCommand(m_set_format.c_str(),
                                                 rkey.data(), rkey.size(),
                                                 reinterpret_cast<const char*>(GWBUF_DATA(pClone)),
                                                 GWBUF_LENGTH(pClone));
        mxb_assert(rc == REDIS_OK);

        // Commit the transaction, will actually be sent only when we ask for the reply.
        MXB_AT_DEBUG(rc =) m_redis.appendCommand("EXEC");
        mxb_assert(rc == REDIS_OK);

        // This will be the response to MULTI above.
        if (m_redis.expect_status("OK", "MULTI"))
        {
            // All commands before EXEC should only return a status of QUEUED.
            m_redis.expect_n_status(n + 1, "QUEUED", "queued command");

            // The reply to EXEC
            Redis::Reply reply;
            rc = m_redis.getReply(&reply);

            if (rc == REDIS_OK)
            {
                if (reply.is_nil())
                {
                    // This *may* happen if WATCH is used, but since we are not, it should not.
                    mxb_assert(!true);
                    action = RedisAction::RETRY;
                }
                else
                {
                    // The reply will now contain the actual responses to the commands
                    // issued after MULTI.
                    mxb_assert(reply.is_array());
                    mxb_assert(reply.elements() == n + 1);

                    Redis::Reply element;

#ifdef SS_DEBUG
                    for (size_t i = 0; i < n; ++i)
                    {
                        element = reply.element(i);
                        mxb_assert(element.is_integer());
                    }
#endif

                    // Then the SET
                    element = reply.element(n);
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

        int rc;
        size_t n = words.size();
        if (n != 0)
        {
            // For each invalidation word (aka table name) we fetch all
            // keys.
            for (size_t i = 0; i < n; ++i)
            {
                const char* pSet = words[i].c_str();
                int set_len = words[i].length();

                // redisAppendCommand can only fail if we run out of memory
                // or if the format string is broken.
                MXB_AT_DEBUG(rc =) m_redis.appendCommand("SMEMBERS %b",
                                                         pSet, set_len);
                mxb_assert(rc == REDIS_OK);
            }
        }

        // Then we iterate over the replies and build one DEL command for
        // deleting all values and one SREM for each invalidation word for
        // deleting the keys of each word.

        vector<Redis::Reply> to_free;

        vector<vector<const char*>> srem_argvs;
        vector<vector<size_t>> srem_argvlens;

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
                    vector<const char*> srem_argv;
                    vector<size_t> srem_argvlen;

                    srem_argv.push_back("SREM");
                    srem_argvlen.push_back(4);

                    srem_argv.push_back(words[i].c_str());
                    srem_argvlen.push_back(words[i].length());

                    for (size_t j = 0; j < reply.elements(); ++j)
                    {
                        Redis::Reply element = reply.element(j);

                        if (element.is_string())
                        {
                            del_argv.push_back(element.str());
                            del_argvlen.push_back(element.len());

                            srem_argv.push_back(element.str());
                            srem_argvlen.push_back(element.len());
                        }
                        else
                        {
                            MXS_ERROR("Unexpected type returned by redis: %s",
                                      redis_type_to_string(element.type()));
                        }
                    }

                    srem_argvs.push_back(std::move(srem_argv));
                    srem_argvlens.push_back(std::move(srem_argvlen));
                }

                to_free.emplace_back(std::move(reply));
            }
            else
            {
                MXS_ERROR("Could not read redis reply for set update for '%s': %s, %s",
                          words[i].c_str(),
                          redis_error_to_string(rc).c_str(),
                          m_redis.errstr());
            }
        }

        if (del_argv.size() > 1)
        {
            rc = m_redis.appendCommand("MULTI");
            mxb_assert(rc == REDIS_OK);

            // Delete the relevant keys from the sets.
            for (size_t i = 0; i < srem_argvs.size(); ++i)
            {
                // Delete keys related to a particular table, the SREM commands.
                const vector<const char*>& srem_argv = srem_argvs[i];
                const vector<size_t>& srem_argvlen = srem_argvlens[i];

                if (srem_argv.size() > 2)
                {
                    const char** ppSrem_argv = const_cast<const char**>(srem_argv.data());
                    MXB_AT_DEBUG(rc =) m_redis.appendCommandArgv(srem_argv.size(),
                                                                 ppSrem_argv,
                                                                 srem_argvlen.data());
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
                m_redis.expect_n_status(srem_argvs.size() + 1, "QUEUED", "queued command");

                // The reply to EXEC
                Redis::Reply reply;
                rc = m_redis.getReply(&reply);

                if (rc == REDIS_OK)
                {
                    if (reply.is_nil())
                    {
                        // This *may* happen if WATCH is used, but since we are not, it should not.
                        mxb_assert(!true);
                        action = RedisAction::RETRY;
                    }
                    else
                    {
                        // The reply will not contain the actual responses to the commands
                        // issued after MULTI.
                        mxb_assert(reply.is_array());
                        mxb_assert(reply.elements() == srem_argvs.size() + 1);

#ifdef SS_DEBUG
                        Redis::Reply element;
                        // Then we handle the replies to the "SREM" commands.
                        for (size_t i = 0; i < srem_argvs.size(); ++i)
                        {
                            element = reply.element(i);
                            mxb_assert(element.is_integer());
                        }

                        // Finally the DEL itself.
                        element = reply.element(srem_argvs.size());
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

private:
    RedisToken(const string& host,
               int port,
               std::chrono::milliseconds timeout,
               bool invalidate,
               uint32_t ttl)
        : m_host(host)
        , m_port(port)
        , m_timeout(timeout)
        , m_pWorker(mxb::Worker::get_current())
        , m_invalidate(invalidate)
        , m_set_format("SET %b %b")
    {
        if (ttl != 0)
        {
            m_set_format += " PX ";
            m_set_format += std::to_string(ttl);
        }
    }

    bool connected() const
    {
        return m_redis.connected();
    }

    void set_context(redisContext* pContext)
    {
        mxb_assert(m_connecting);

        if (pContext)
        {
            if (pContext->err != 0)
            {
                MXS_ERROR("%s. Is the address '%s:%d' valid? Caching will not be enabled.",
                          pContext->errstr ? pContext->errstr : "Could not connect to redis",
                          m_host.c_str(), m_port);
            }
        }
        else
        {
            MXS_ERROR("Could not create Redis handle. Caching will not be enabled.");
        }

        m_redis.reset(pContext);

        if (connected())
        {
            if (m_reconnecting)
            {
                // Reconnected after having been disconnected, let's log a note.
                MXS_NOTICE("Connected to Redis storage. Caching is enabled.");
            }
        }

        m_context_got = std::chrono::steady_clock::now();
        m_connecting = false;
        m_reconnecting = false;
    }

    void connect()
    {
        mxb_assert(!m_connecting);
        m_connecting = true;

        auto sThis = get_shared();

        auto host = m_host;
        auto port = m_port;
        auto timeout = m_timeout;

        mxs::thread_pool().execute([sThis, host, port, timeout] () {
                auto milliseconds = timeout.count();
                timeval tv;
                tv.tv_sec = milliseconds / 1000;
                tv.tv_usec = milliseconds - (tv.tv_sec * 1000);

                redisContext* pContext = redisConnectWithTimeout(host.c_str(), port, tv);

                sThis->m_pWorker->execute([sThis, pContext]() {
                        if (sThis.use_count() > 1) // The session is still alive
                        {
                            sThis->set_context(pContext);
                        }
                        else
                        {
                            redisFree(pContext);
                        }
                    }, mxb::Worker::EXECUTE_QUEUED);
            });
    }

    void reconnect()
    {
        if (!m_connecting)
        {
            m_reconnecting = true;

            auto now = std::chrono::steady_clock::now();

            if (now - m_context_got > m_timeout)
            {
                connect();
            }
        }
    }

private:
    Redis                                 m_redis;
    string                                m_host;
    int                                   m_port;
    std::chrono::milliseconds             m_timeout;
    mxb::Worker*                          m_pWorker;
    bool                                  m_invalidate;
    std::string                           m_set_format;
    std::chrono::steady_clock::time_point m_context_got;
    bool                                  m_connecting { false };
    bool                                  m_reconnecting { false };
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
                                   const std::string& argument_string)
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

    map<string, string> arguments;

    if (Storage::split_arguments(argument_string, &arguments))
    {
        bool error = false;

        mxb::Host host;

        auto it = arguments.find(CN_STORAGE_ARG_SERVER);

        if (it != arguments.end())
        {
            if (!Storage::get_host(it->second, DEFAULT_REDIS_PORT, &host))
            {
                error = true;
            }

            arguments.erase(it);
        }
        else
        {
            MXS_ERROR("The mandatory argument '%s' is missing.", CN_STORAGE_ARG_SERVER);
            error = true;
        }

        for (const auto& kv : arguments)
        {
            MXS_WARNING("Unknown `storage_redis` argument: %s=%s",
                        kv.first.c_str(), kv.second.c_str());
        }

        if (!error)
        {
            pStorage = new (std::nothrow) RedisStorage(name, config, host.address(), host.port());
        }
    }

    return pStorage;
}

bool RedisStorage::create_token(shared_ptr<Storage::Token>* psToken)
{
    return RedisToken::create(m_host, m_port, m_config.timeout, m_invalidate, m_ttl, psToken);
}

void RedisStorage::get_config(Config* pConfig)
{
    *pConfig = m_config;
}

void RedisStorage::get_limits(Limits* pLimits)
{
    *pLimits = this_unit.default_limits;
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
                                       const std::function<void (cache_result_t, GWBUF*)>& cb)
{
    mxb_assert(pToken);

    return static_cast<RedisToken*>(pToken)->get_value(key, flags, soft_ttl, hard_ttl, ppValue, cb);
}

cache_result_t RedisStorage::put_value(Token* pToken,
                                       const CacheKey& key,
                                       const vector<string>& invalidation_words,
                                       const GWBUF* pValue,
                                       const std::function<void (cache_result_t)>& cb)
{
    mxb_assert(pToken);

    return static_cast<RedisToken*>(pToken)->put_value(key, invalidation_words, pValue, cb);
}

cache_result_t RedisStorage::del_value(Token* pToken,
                                       const CacheKey& key,
                                       const std::function<void (cache_result_t)>& cb)
{
    mxb_assert(pToken);

    return static_cast<RedisToken*>(pToken)->del_value(key, cb);
}

cache_result_t RedisStorage::invalidate(Token* pToken,
                                        const vector<string>& words,
                                        const std::function<void (cache_result_t)>& cb)
{
    mxb_assert(pToken);

    return static_cast<RedisToken*>(pToken)->invalidate(words, cb);
}

cache_result_t RedisStorage::clear(Token* pToken)
{
    mxb_assert(pToken);

    return static_cast<RedisToken*>(pToken)->clear();
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
