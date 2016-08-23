/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <stdio.h>
#include <filter.h>
#include <modinfo.h>
#include <modutil.h>
#include <atomic.h>
#include <query_classifier.h>
#include <hashtable.h>
#include <gwdirs.h>
#include <maxscale/alloc.h>

#define GK_DEFAULT_HASHTABLE_SIZE 1000

/**
 * @file gatekeeper.c - A learning firewall
 *
 * This filter will learn from input data read during a learning phase.
 * After learning the characteristics of the input, the filter can then
 * be set into an enforcing mode. In this mode the filter will block any
 * queries that do not conform to the training set.
 */

MODULE_INFO info =
{
    MODULE_API_FILTER,
    MODULE_ALPHA_RELEASE,
    FILTER_VERSION,
    "Learning firewall filter"
};

enum firewall_mode
{
    ENFORCE,
    LEARN
};

typedef struct
{
    unsigned long queries; /**< Number of queries received */
    unsigned int hit; /**< Number of queries that match a pattern */
    unsigned int miss; /**< Number of queries that do not match a pattern */
    unsigned int entries; /**< Number of new patterns created */
} GK_STATS;

typedef struct
{
    HASHTABLE *queryhash; /**< Canonicalized forms of the queries */
    char* datadir; /**< The data is stored in this directory as lfw.data */
    enum firewall_mode mode; /**< Filter mode, either ENFORCE or LEARN */
    GK_STATS stats; /**< Instance statistics */
    SPINLOCK lock; /**< Instance lock */
    bool updating; /**< If the datafile is being updated */
    bool need_update; /**< If the datafile needs updating */
} GK_INSTANCE;

typedef struct
{
    DCB* dcb; /**< Client DCB, used to send error messages */
    DOWNSTREAM down;
    GK_STATS stats; /**< Session statistics */
} GK_SESSION;

static char *version_str = "V1.0.0";
static const char* datafile_name = "gatekeeper.data";

/** Prefix all log messages with this tag */
#define MODNAME "[gatekeeper] "

/** This is passed as a value to @c hashtable_add to have @c hashtable_fetch
 * return a non-NULL value when a hash hit is made */
static bool trueval = true;

static FILTER *createInstance(const char *name, char **options, FILTER_PARAMETER **params);
static void *newSession(FILTER *instance, SESSION *session);
static void closeSession(FILTER *instance, void *session);
static void freeSession(FILTER *instance, void *session);
static void setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static int routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static void diagnostic(FILTER *instance, void *fsession, DCB *dcb);
static bool read_stored_data(GK_INSTANCE *inst);
static bool write_stored_data(GK_INSTANCE *inst);

static FILTER_OBJECT MyObject =
{
    createInstance,
    newSession,
    closeSession,
    freeSession,
    setDownstream,
    NULL, // No upstream requirement
    routeQuery,
    NULL,
    diagnostic,
};

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char* version()
{
    return version_str;
}

/**
 * The module initialization routine, called when the module
 * is first loaded.
 */
void ModuleInit()
{
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
FILTER_OBJECT* GetModuleObject()
{
    return &MyObject;
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 *
 * @param name      The name of the instance (as defined in the config file).
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static FILTER* createInstance(const char *name, char **options, FILTER_PARAMETER **params)
{
    GK_INSTANCE *inst = MXS_CALLOC(1, sizeof(GK_INSTANCE));

    if (inst)
    {
        const char* datadir = get_datadir();
        bool ok = true;
        spinlock_init(&inst->lock);
        inst->mode = LEARN;

        for (int i = 0; params && params[i]; i++)
        {
            if (strcmp(params[i]->name, "mode") == 0)
            {
                if (strcasecmp(params[i]->value, "enforce") == 0)
                {
                    inst->mode = ENFORCE;
                }
                else if (strcasecmp(params[i]->value, "learn") == 0)
                {
                    inst->mode = LEARN;
                }
                else
                {
                    MXS_ERROR(MODNAME"Unknown value for 'mode': %s", params[i]->value);
                    ok = false;
                }
            }
            else if (strcmp(params[i]->name, "datadir") == 0)
            {
                if (access(params[i]->value, F_OK) == 0)
                {
                    datadir = params[i]->value;
                }
                else
                {
                    char err[STRERROR_BUFLEN];
                    MXS_ERROR(MODNAME"File is not accessible: %d, %s", errno,
                              strerror_r(errno, err, sizeof(err)));
                    ok = false;
                }
            }
            else if (!filter_standard_parameter(params[i]->name))
            {
                MXS_ERROR(MODNAME"Unknown parameter '%s'.", params[i]->name);
                ok = false;
            }
        }

        if (ok)
        {
            inst->queryhash = hashtable_alloc(GK_DEFAULT_HASHTABLE_SIZE,
                                              hashtable_item_strhash,
                                              hashtable_item_strcasecmp);
            inst->datadir = MXS_STRDUP(datadir);
            if (inst->queryhash && inst->datadir)
            {
                hashtable_memory_fns(inst->queryhash, NULL, NULL, hashtable_item_free, NULL);
                if (read_stored_data(inst))
                {
                    MXS_NOTICE(MODNAME"Started in [%s] mode. Data is stored at: %s",
                               inst->mode == ENFORCE ? "ENFORCE" : "LEARN", inst->datadir);
                }
                else
                {
                    ok = false;
                }
            }
            else
            {
                ok = false;
            }
        }

        if (!ok)
        {
            hashtable_free(inst->queryhash);
            MXS_FREE(inst->datadir);
            MXS_FREE(inst);
            inst = NULL;
        }
    }

    return (FILTER*)inst;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static void* newSession(FILTER *instance, SESSION *session)
{
    GK_INSTANCE *inst = (GK_INSTANCE *) instance;
    GK_SESSION *ses;

    if ((ses = MXS_CALLOC(1, sizeof(GK_SESSION))) != NULL)
    {
        ses->dcb = session->client_dcb;
    }

    return ses;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * The gatekeeper flushes the hashtable to disk every time a session is closed.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void closeSession(FILTER *instance, void *session)
{
    GK_INSTANCE *inst = (GK_INSTANCE*) instance;
    GK_SESSION *ses = (GK_SESSION *) session;

    /** If we added new data into the queryhash, update the datafile on disk */
    if (ses->stats.entries > 0)
    {
        spinlock_acquire(&inst->lock);

        bool update = !inst->updating;
        if (update)
        {
            inst->updating = true;
            inst->need_update = false;
        }
        else
        {
            /** If another thread is already updating the file, set
             * the need_update flag */
            inst->need_update = true;
        }

        spinlock_release(&inst->lock);

        while (update)
        {
            /** Store the hashtable to disk */
            write_stored_data(inst);

            spinlock_acquire(&inst->lock);

            /** Check if the hashtable has been update while we were writing
             * the data to disk. */
            if ((update = inst->need_update))
            {
                inst->need_update = false;
            }
            else
            {
                inst->updating = false;
            }

            spinlock_release(&inst->lock);
        }
    }

    /** Add session stats to instance stats */
    spinlock_acquire(&inst->lock);
    inst->stats.entries += ses->stats.entries;
    inst->stats.hit += ses->stats.hit;
    inst->stats.miss += ses->stats.miss;
    inst->stats.queries += ses->stats.queries;
    spinlock_release(&inst->lock);
}

/**
 * Free the memory associated with this filter session.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void freeSession(FILTER *instance, void *session)
{
    MXS_FREE(session);
}

/**
 * Set the downstream component for this filter.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 * @param downstream    The downstream filter or router
 */
static void setDownstream(FILTER *instance, void *session, DOWNSTREAM *downstream)
{
    GK_SESSION *ses = (GK_SESSION *) session;
    ses->down = *downstream;
}

/**
 * @brief Main routing function
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param queue     The query data
 * @return 1 on success, 0 on error
 */
static int routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
    GK_INSTANCE *inst = (GK_INSTANCE*) instance;
    GK_SESSION *ses = (GK_SESSION *) session;
    int rval = 1;
    bool ok = true;
    char* canon = qc_get_canonical(queue);

    ses->stats.queries++;

    /** Non-COM_QUERY packets are better handled on the backend database. For
     * example a COM_INIT_DB does not get canonicalized and would be always
     * denied. For this reason, queries that are not canonicalized are allowed.
     * This means that the binary protocol and prepared statements are not
     * processed by this filter. */
    if (canon)
    {
        if (inst->mode == ENFORCE)
        {
            if (hashtable_fetch(inst->queryhash, canon))
            {
                ses->stats.hit++;
            }
            else
            {
                ses->stats.miss++;
                ok = false;
                MXS_WARNING(MODNAME"Query by %s@%s was not found from queryhash: %s",
                            ses->dcb->user, ses->dcb->remote, canon);
                GWBUF* errbuf = modutil_create_mysql_err_msg(1, 0, 1, "00000", "Permission denied.");
                rval = errbuf ? ses->dcb->func.write(ses->dcb, errbuf) : 0;
            }
            MXS_FREE(canon);
        }
        else if (inst->mode == LEARN)
        {
            if (hashtable_add(inst->queryhash, canon, &trueval))
            {
                ses->stats.entries++;
            }
            else
            {
                MXS_FREE(canon);
            }
        }
    }

    if (ok)
    {
        rval = ses->down.routeQuery(ses->down.instance, ses->down.session, queue);
    }

    return rval;
}

/**
 * @brief Diagnostics routine
 *
 * @param   instance    The filter instance
 * @param   fsession    Filter session
 * @param   dcb         The DCB for output
 */
static void diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
    GK_INSTANCE *inst = (GK_INSTANCE *) instance;

    dcb_printf(dcb, "\t\tQueries: %lu\n", inst->stats.queries);
    dcb_printf(dcb, "\t\tQueryhash entries: %u\n", inst->stats.entries);
    dcb_printf(dcb, "\t\tQueryhash hits: %u\n", inst->stats.hit);
    dcb_printf(dcb, "\t\tQueryhash misses: %u\n", inst->stats.miss);
}

/**
 * @brief Write query patterns from memory to disk
 *
 * The data is stored as length-encoded strings. A length-encoded string contains
 * a 4 byte integer, which tells the length of the string, followed by the string
 * itself. The stored file will consist of multiple consecutive length-encoded strings.
 *
 * @param inst Filter instance
 * @return True on success
 */
static bool write_stored_data(GK_INSTANCE *inst)
{
    bool rval = false;
    char filepath[PATH_MAX];
    sprintf(filepath, "%s/%s.tmp.XXXXXX", inst->datadir, datafile_name);
    int fd = mkstemp(filepath);
    HASHITERATOR *iter = hashtable_iterator(inst->queryhash);

    if (fd > 0 && iter)
    {
        char *key;
        bool ok = true;

        while ((key = hashtable_next(iter)))
        {
            uint32_t len = strlen(key);

            /** First write the length of the string and then the string itself */
            if (write(fd, &len, sizeof(len)) != sizeof(len) ||
                write(fd, key, len) != len)
            {
                char err[STRERROR_BUFLEN];
                MXS_ERROR(MODNAME"Failed to write key '%s' to disk (%d, %s). The datafile at '%s' was "
                          "not updated but it will be updated when the next session closes. ",
                          key, errno, strerror_r(errno, err, sizeof(err)), inst->datadir);
                ok = false;
                break;
            }
        }

        if (ok)
        {
            /** Update the file by renaming the temporary file to the original one*/
            char newfilepath[PATH_MAX + 1];
            snprintf(newfilepath, sizeof(newfilepath), "%s/%s", inst->datadir, datafile_name);
            rval = rename(filepath, newfilepath) == 0;

            if (!rval)
            {
                char err[STRERROR_BUFLEN];
                MXS_ERROR(MODNAME"Failed to rename file '%s' to '%s' when writing data: %d, %s",
                          filepath, newfilepath, errno, strerror_r(errno, err, sizeof(err)));
            }
        }
    }
    else if (fd == -1)
    {
        char err[STRERROR_BUFLEN];
        MXS_ERROR(MODNAME"Failed to open file '%s' when writing data: %d, %s",
                  filepath, errno, strerror_r(errno, err, sizeof(err)));
    }

    if (fd > 0)
    {
        close(fd);
    }
    hashtable_iterator_free(iter);

    return rval;
}

static void report_failed_read(FILE *file, int nexpected, int nread)
{
    if (ferror(file))
    {
        char err[STRERROR_BUFLEN];
        MXS_ERROR(MODNAME"Failed to read %d bytes, only %d bytes read: %d, %s",
                  nexpected, nread, errno, strerror_r(errno, err, sizeof(err)));
    }
    else
    {
        MXS_ERROR(MODNAME"Partial read, expected %d bytes but read only %d.",
                  nexpected, nread);
    }
}

/**
 * @brief Read query patterns from disk to memory
 *
 * See write_stored_data() for details on how the data is stored.
 *
 * @param inst Filter instance
 * @return True if data was successfully read
 */
static bool read_stored_data(GK_INSTANCE *inst)
{
    char filepath[PATH_MAX + 1];
    snprintf(filepath, sizeof(filepath), "%s/%s", inst->datadir, datafile_name);

    if (access(filepath, F_OK) != 0)
    {
        if (inst->mode == ENFORCE)
        {
            MXS_ERROR(MODNAME"Started in ENFORCE mode but no datafile was found at '%s'.", filepath);
            return false;
        }

        /** Not finding a datafile in LEARN mode is OK since it will be created later on */
        return true;
    }

    bool rval = true;
    FILE *file = fopen(filepath, "rb");

    if (file)
    {
        do
        {
            uint32_t len;
            size_t nread;
            char *data;

            /** Read the length of the string */
            if ((nread = fread(&len, 1, sizeof(len), file)) != sizeof(len))
            {
                if (nread > 0 || !feof(file))
                {
                    report_failed_read(file, sizeof(len), nread);
                    rval = false;
                }
                break;
            }

            if ((data = MXS_MALLOC(len + 1)) == NULL)
            {
                rval = false;
                break;
            }

            /** Read the string itself */
            if ((nread = fread(data, 1, len, file)) != len)
            {
                MXS_FREE(data);
                report_failed_read(file, sizeof(len), nread);
                rval = false;
                break;
            }

            data[len] = '\0';
            hashtable_add(inst->queryhash, data, &trueval);
        }
        while (!feof(file));

        fclose(file);
    }
    else
    {
        char err[STRERROR_BUFLEN];
        MXS_ERROR(MODNAME"Failed to open file '%s' when reading stored data: %d, %s",
                  filepath, errno, strerror_r(errno, err, sizeof(err)));
    }

    return rval;
}
