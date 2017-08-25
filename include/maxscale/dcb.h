#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file dcb.h  The Descriptor Control Block
 */

#include <maxscale/cdefs.h>
#include <maxscale/spinlock.h>
#include <maxscale/buffer.h>
#include <maxscale/protocol.h>
#include <maxscale/authenticator.h>
#include <maxscale/ssl.h>
#include <maxscale/modinfo.h>
#include <maxscale/poll_core.h>
#include <netinet/in.h>

MXS_BEGIN_DECLS

#define ERRHANDLE

struct session;
struct server;
struct service;
struct servlistener;

struct dcb;

#define DCBFD_CLOSED -1

/**
 * The statistics gathered on a descriptor control block
 */
typedef struct dcbstats
{
    int     n_reads;        /*< Number of reads on this descriptor */
    int     n_writes;       /*< Number of writes on this descriptor */
    int     n_accepts;      /*< Number of accepts on this descriptor */
    int     n_buffered;     /*< Number of buffered writes */
    int     n_high_water;   /*< Number of crosses of high water mark */
    int     n_low_water;    /*< Number of crosses of low water mark */
} DCBSTATS;

#define DCBSTATS_INIT {0}

/* DCB states */
typedef enum
{
    DCB_STATE_UNDEFINED,    /*< State variable with no state */
    DCB_STATE_ALLOC,        /*< Memory allocated but not populated */
    DCB_STATE_POLLING,      /*< Waiting in the poll loop */
    DCB_STATE_WAITING,      /*< Client wanting a connection */
    DCB_STATE_LISTENING,    /*< The DCB is for a listening socket */
    DCB_STATE_DISCONNECTED, /*< The socket is now closed */
    DCB_STATE_NOPOLLING,    /*< Removed from poll mask */
} dcb_state_t;

typedef enum
{
    DCB_ROLE_SERVICE_LISTENER,      /*< Receives initial connect requests from clients */
    DCB_ROLE_CLIENT_HANDLER,        /*< Serves dedicated client */
    DCB_ROLE_BACKEND_HANDLER,       /*< Serves back end connection */
    DCB_ROLE_INTERNAL               /*< Internal DCB not connected to the outside */
} dcb_role_t;

#define DCB_STRTYPE(dcb) (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER ? "Client DCB" : \
                          dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER ? "Backend DCB" : \
                          dcb->dcb_role == DCB_ROLE_SERVICE_LISTENER ? "Listener DCB" : \
                          dcb->dcb_role == DCB_ROLE_INTERNAL ? "Internal DCB" : "Unknown DCB")

/**
 * Callback reasons for the DCB callback mechanism.
 */
typedef enum
{
    DCB_REASON_CLOSE,               /*< The DCB is closing */
    DCB_REASON_DRAINED,             /*< The write delay queue has drained */
    DCB_REASON_HIGH_WATER,          /*< Cross high water mark */
    DCB_REASON_LOW_WATER,           /*< Cross low water mark */
    DCB_REASON_ERROR,               /*< An error was flagged on the connection */
    DCB_REASON_HUP,                 /*< A hangup was detected */
    DCB_REASON_NOT_RESPONDING       /*< Server connection was lost */
} DCB_REASON;

/**
 * Callback structure - used to track callbacks registered on a DCB
 */
typedef struct dcb_callback
{
    DCB_REASON           reason;         /*< The reason for the callback */
    int                 (*cb)(struct dcb *dcb, DCB_REASON reason, void *userdata);
    void                 *userdata;      /*< User data to be sent in the callback */
    struct dcb_callback  *next;          /*< Next callback for this DCB */
} DCB_CALLBACK;

/**
 * State of SSL connection
 */
typedef enum
{
    SSL_HANDSHAKE_UNKNOWN,          /*< The DCB has unknown SSL status */
    SSL_HANDSHAKE_REQUIRED,         /*< SSL handshake is needed */
    SSL_HANDSHAKE_DONE,             /*< The SSL handshake completed OK */
    SSL_ESTABLISHED,                /*< The SSL connection is in use */
    SSL_HANDSHAKE_FAILED            /*< The SSL handshake failed */
} SSL_STATE;

/**
 * Descriptor Control Block
 *
 * A wrapper for a network descriptor within the gateway, it contains all the
 * state information necessary to allow for the implementation of the asynchronous
 * operation of the protocol and gateway functions. It also provides links to the service
 * and session data that is required to route the information within the gateway.
 *
 * It is important to hold the state information here such that any thread within the
 * gateway may be selected to execute the required actions when a network event occurs.
 *
 * Note that the first few fields (up to and including "entry_is_ready") must
 * precisely match the LIST_ENTRY structure defined in the list manager.
 */
typedef struct dcb
{
    MXS_POLL_DATA   poll;
    skygw_chk_t     dcb_chk_top;
    bool            dcb_errhandle_called; /*< this can be called only once */
    dcb_role_t      dcb_role;
    int             fd;             /**< The descriptor */
    dcb_state_t     state;          /**< Current descriptor state */
    SSL_STATE       ssl_state;      /**< Current state of SSL if in use */
    int             flags;          /**< DCB flags */
    char            *remote;        /**< Address of remote end */
    char            *user;          /**< User name for connection */
    struct sockaddr_storage ip;     /**< remote IPv4/IPv6 address */
    char            *protoname;     /**< Name of the protocol */
    void            *protocol;      /**< The protocol specific state */
    size_t           protocol_packet_length; /**< How long the protocol specific packet is */
    size_t           protocol_bytes_processed; /**< How many bytes of a packet have been read */
    struct session  *session;       /**< The owning session */
    struct servlistener *listener;  /**< For a client DCB, the listener data */
    MXS_PROTOCOL    func;           /**< The protocol functions for this descriptor */
    MXS_AUTHENTICATOR authfunc;     /**< The authenticator functions for this descriptor */
    int             writeqlen;      /**< Current number of byes in the write queue */
    GWBUF           *writeq;        /**< Write Data Queue */
    GWBUF           *delayq;        /**< Delay Backend Write Data Queue */
    GWBUF           *dcb_readqueue; /**< read queue for storing incomplete reads */
    GWBUF           *dcb_fakequeue; /**< Fake event queue for generated events */

    DCBSTATS        stats;          /**< DCB related statistics */
    struct dcb      *nextpersistent;   /**< Next DCB in the persistent pool for SERVER */
    time_t          persistentstart;   /**<    0: Not in the persistent pool.
                                              -1: Evicted from the persistent pool and being closed.
                                           non-0: Time when placed in the persistent pool.
                                       */
    struct service  *service;       /**< The related service */
    void            *data;          /**< Specific client data, shared between DCBs of this session */
    void            *authenticator_data; /**< The authenticator data for this DCB */
    DCB_CALLBACK    *callbacks;     /**< The list of callbacks for the DCB */
    int64_t         last_read;      /*< Last time the DCB received data */
    struct server   *server;        /**< The associated backend server */
    SSL*            ssl;            /*< SSL struct for connection */
    bool            ssl_read_want_read;    /*< Flag */
    bool            ssl_read_want_write;    /*< Flag */
    bool            ssl_write_want_read;    /*< Flag */
    bool            ssl_write_want_write;    /*< Flag */
    bool            was_persistent;  /**< Whether this DCB was in the persistent pool */
    struct
    {
        struct dcb *next; /**< Next DCB in owning thread's list */
        struct dcb *tail; /**< Last DCB in owning thread's list */
    } thread;
    skygw_chk_t     dcb_chk_tail;
} DCB;

/**
 * The DCB usage filer used for returning DCB's in use for a certain reason
 */
typedef enum
{
    DCB_USAGE_CLIENT,
    DCB_USAGE_LISTENER,
    DCB_USAGE_BACKEND,
    DCB_USAGE_INTERNAL,
    DCB_USAGE_ALL
} DCB_USAGE;

/* A few useful macros */
#define DCB_SESSION(x)                  (x)->session
#define DCB_PROTOCOL(x, type)           (type *)((x)->protocol)
#define DCB_WRITEQLEN(x)                (x)->writeqlen
#define DCB_SET_LOW_WATER(x, lo)        (x)->low_water = (lo);
#define DCB_SET_HIGH_WATER(x, hi)       (x)->low_water = (hi);
#define DCB_BELOW_LOW_WATER(x)          ((x)->low_water && (x)->writeqlen < (x)->low_water)
#define DCB_ABOVE_HIGH_WATER(x)         ((x)->high_water && (x)->writeqlen > (x)->high_water)

/**
 * @brief DCB system initialization function
 *
 * This function needs to be the first function call into this system.
 */
void dcb_global_init();

int dcb_write(DCB *, GWBUF *);
DCB *dcb_accept(DCB *listener);
DCB *dcb_alloc(dcb_role_t, struct servlistener *);
void dcb_free(DCB *);
void dcb_free_all_memory(DCB *dcb);
DCB *dcb_connect(struct server *, struct session *, const char *);
int dcb_read(DCB *, GWBUF **, int);
int dcb_drain_writeq(DCB *);
void dcb_close(DCB *);

/**
 * Add a DCB to the owner's list
 *
 * @param dcb DCB to add
 */
void dcb_add_to_list(DCB *dcb);

void printAllDCBs();                         /* Debug to print all DCB in the system */
void printDCB(DCB *);                        /* Debug print routine */
void dprintDCBList(DCB *);                 /* Debug print DCB list statistics */
void dprintAllDCBs(DCB *);                   /* Debug to print all DCB in the system */
void dprintOneDCB(DCB *, DCB *);             /* Debug to print one DCB */
void dprintDCB(DCB *, DCB *);                /* Debug to print a DCB in the system */
void dListDCBs(DCB *);                       /* List all DCBs in the system */
void dListClients(DCB *);                    /* List al the client DCBs */
const char *gw_dcb_state2string(dcb_state_t);              /* DCB state to string */
void dcb_printf(DCB *, const char *, ...) __attribute__((format(printf, 2, 3))); /* DCB version of printf */
void dcb_hashtable_stats(DCB *, void *);     /**< Print statisitics */
int dcb_add_callback(DCB *, DCB_REASON, int (*)(struct dcb *, DCB_REASON, void *), void *);
int dcb_remove_callback(DCB *, DCB_REASON, int (*)(struct dcb *, DCB_REASON, void *), void *);
int dcb_isvalid(DCB *);                     /* Check the DCB is in the linked list */
int dcb_count_by_usage(DCB_USAGE);          /* Return counts of DCBs */
int dcb_persistent_clean_count(DCB *, int, bool);      /* Clean persistent and return count */
void dcb_hangup_foreach (struct server* server);
uint64_t dcb_get_session_id(DCB* dcb);
char *dcb_role_name(DCB *);                  /* Return the name of a role */
int dcb_accept_SSL(DCB* dcb);
int dcb_connect_SSL(DCB* dcb);
int dcb_listen(DCB *listener, const char *config, const char *protocol_name);
void dcb_append_readqueue(DCB *dcb, GWBUF *buffer);
void dcb_enable_session_timeouts();
void dcb_process_idle_sessions(int thr);

/**
 * @brief Call a function for each connected DCB
 *
 * @deprecated You should not use this function, use dcb_foreach_parallel instead
 *
 * @param func Function to call. The function should return @c true to continue iteration
 * and @c false to stop iteration earlier. The first parameter is a DCB and the second
 * is the value of @c data that the user provided.
 * @param data User provided data passed as the second parameter to @c func
 * @return True if all DCBs were iterated, false if the callback returned false
 */
bool dcb_foreach(bool (*func)(DCB *dcb, void *data), void *data);

/**
 * @brief Call a function for each connected DCB
 *
 * @note This function can call @c func from multiple thread at one time.
 *
 * @param func Function to call. The function should return @c true to continue iteration
 *             and @c false to stop iteration earlier. The first is a DCB and
 *             the second is this thread's value in the @c data array that
 *             the user provided.
 *
 * @param data Array of user provided data passed as the second parameter to @c func.
 *             The array must have more space for pointers thann the return
 *             value of `config_threadcount()`. The value passed to @c func will
 *             be the value of the array at the index of the current thread's ID.
 */
void dcb_foreach_parallel(bool (*func)(DCB *dcb, void *data), void **data);

/**
 * @brief Return the port number this DCB is connected to
 *
 * @param dcb DCB to inspect
 * @return Port number the DCB is connected to or -1 if information is not available
 */
int dcb_get_port(const DCB *dcb);

/**
 * DCB flags values
 */
#define DCBF_HUNG               0x0002  /*< Hangup has been dispatched */
#define DCBF_REPLIED    0x0004  /*< DCB was written to */

#define DCB_REPLIED(d) ((d)->flags & DCBF_REPLIED)

MXS_END_DECLS
