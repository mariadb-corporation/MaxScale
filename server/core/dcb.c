/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file dcb.c  -  Descriptor Control Block generic functions
 *
 * Descriptor control blocks provide the key mechanism for the interface
 * with the non-blocking socket polling routines. The descriptor control
 * block is the user data that is handled by the epoll system and contains
 * the state data and pointers to other components that relate to the
 * use of a file descriptor.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 12/06/13     Mark Riddoch            Initial implementation
 * 21/06/13     Massimiliano Pinto      free_dcb is used
 * 25/06/13     Massimiliano Pinto      Added checks to session and router_session
 * 28/06/13     Mark Riddoch            Changed the free mechanism to
 *                                      introduce a zombie state for the
 *                                      dcb
 * 02/07/2013   Massimiliano Pinto      Addition of delayqlock, delayq and
 *                                      authlock for handling backend
 *                                      asynchronous protocol connection
 *                                      and a generic lock for backend
 *                                      authentication
 * 16/07/2013   Massimiliano Pinto      Added command type for dcb
 * 23/07/2013   Mark Riddoch            Tidy up logging
 * 02/09/2013   Massimiliano Pinto      Added session refcount
 * 27/09/2013   Massimiliano Pinto      dcb_read returns 0 if ioctl returns no
 *                                      error and 0 bytes to read.
 *                                      This fixes a bug with many reads from
 *                                      backend
 * 07/05/2014   Mark Riddoch            Addition of callback mechanism
 * 20/06/2014   Mark Riddoch            Addition of dcb_clone
 * 29/05/2015   Markus Makela           Addition of dcb_write_SSL
 * 11/06/2015   Martin Brampton         Persistent connnections and tidy up
 * 07/07/2015   Martin Brampton         Merged add to zombieslist into dcb_close,
 *                                      fixes for various error situations,
 *                                      remove dcb_set_state etc, simplifications.
 * 10/07/2015   Martin Brampton         Simplify, merge dcb_read and dcb_read_n
 * 04/09/2015   Martin Brampton         Changes to ensure DCB always has session pointer
 * 28/09/2015   Martin Brampton         Add counters, maxima for DCBs and zombies
 * 29/05/2015   Martin Brampton         Impose locking in dcb_call_foreach callbacks
 * 17/10/2015   Martin Brampton         Add hangup for each and bitmask display MaxAdmin
 * 15/12/2015   Martin Brampton         Merge most of SSL write code into non-SSL,
 *                                      enhance SSL code
 * 07/02/2016   Martin Brampton         Make dcb_read_SSL & dcb_create_SSL internal,
 *                                      further small SSL logic changes
 * 31/05/2016   Martin Brampton         Implement connection throttling
 * 27/06/2016   Martin Brampton         Implement list manager to manage DCB memory
 *
 * @endverbatim
 */
#include <maxscale/dcb.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include <maxscale/spinlock.h>
#include <maxscale/server.h>
#include <maxscale/service.h>
#include <maxscale/router.h>
#include <maxscale/poll.h>
#include <maxscale/atomic.h>
#include <maxscale/limits.h>
#include <maxscale/log_manager.h>
#include <maxscale/hashtable.h>
#include <maxscale/listener.h>
#include <maxscale/hk_heartbeat.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <maxscale/alloc.h>
#include <maxscale/utils.h>
#include <maxscale/platform.h>

#include "maxscale/session.h"
#include "maxscale/modules.h"
#include "maxscale/queuemanager.h"

/* A DCB with null values, used for initialization */
static DCB dcb_initialized = DCB_INIT;

static  DCB           **all_dcbs;
static  SPINLOCK       *all_dcbs_lock;
static  DCB           **zombies;
static  int            *nzombies;
static  int             maxzombies = 0;
static  SPINLOCK        zombiespin = SPINLOCK_INIT;

/** Variables for session timeout checks */
bool check_timeouts = false;
thread_local long next_timeout_check = 0;

void dcb_global_init()
{
    int nthreads = config_threadcount();

    if ((zombies = MXS_CALLOC(nthreads, sizeof(DCB*))) == NULL ||
        (all_dcbs = MXS_CALLOC(nthreads, sizeof(DCB*))) == NULL ||
        (all_dcbs_lock = MXS_CALLOC(nthreads, sizeof(SPINLOCK))) == NULL ||
        (nzombies = MXS_CALLOC(nthreads, sizeof(int))) == NULL)
    {
        MXS_OOM();
        raise(SIGABRT);
    }

    for (int i = 0; i < nthreads; i++)
    {
        spinlock_init(&all_dcbs_lock[i]);
    }
}

static void dcb_initialize(void *dcb);
static void dcb_final_free(DCB *dcb);
static void dcb_call_callback(DCB *dcb, DCB_REASON reason);
static int  dcb_null_write(DCB *dcb, GWBUF *buf);
static int  dcb_null_auth(DCB *dcb, SERVER *server, MXS_SESSION *session, GWBUF *buf);
static inline DCB * dcb_find_in_list(DCB *dcb);
static inline void dcb_process_victim_queue(int threadid);
static void dcb_stop_polling_and_shutdown (DCB *dcb);
static bool dcb_maybe_add_persistent(DCB *);
static inline bool dcb_write_parameter_check(DCB *dcb, GWBUF *queue);
static int dcb_bytes_readable(DCB *dcb);
static int dcb_read_no_bytes_available(DCB *dcb, int nreadtotal);
static int dcb_create_SSL(DCB* dcb, SSL_LISTENER *ssl);
static int dcb_read_SSL(DCB *dcb, GWBUF **head);
static GWBUF *dcb_basic_read(DCB *dcb, int bytesavailable, int maxbytes, int nreadtotal, int *nsingleread);
static GWBUF *dcb_basic_read_SSL(DCB *dcb, int *nsingleread);
static void dcb_log_write_failure(DCB *dcb, GWBUF *queue, int eno);
static inline void dcb_write_tidy_up(DCB *dcb, bool below_water);
static int gw_write(DCB *dcb, GWBUF *writeq, bool *stop_writing);
static int gw_write_SSL(DCB *dcb, GWBUF *writeq, bool *stop_writing);
static int dcb_log_errors_SSL (DCB *dcb, const char *called_by, int ret);
static int dcb_accept_one_connection(DCB *listener, struct sockaddr *client_conn);
static int dcb_listen_create_socket_inet(const char *host, uint16_t port);
static int dcb_listen_create_socket_unix(const char *path);
static int dcb_set_socket_option(int sockfd, int level, int optname, void *optval, socklen_t optlen);
static void dcb_add_to_all_list(DCB *dcb);
static DCB *dcb_find_free();
static GWBUF *dcb_grab_writeq(DCB *dcb, bool first_time);
static void dcb_remove_from_list(DCB *dcb);

size_t dcb_get_session_id(
    DCB *dcb)
{
    return (dcb && dcb->session) ? dcb->session->ses_id : 0;
}

/**
 * @brief Initialize a DCB
 *
 * This routine puts initial values into the fields of the DCB pointed to
 * by the parameter. The parameter has to be passed as void * because the
 * function can be called by the generic list manager, which does not know
 * the actual type of the list entries it handles.
 *
 * Most fields can be initialized by the assignment of the static
 * initialized DCB. The exception is the bitmask.
 *
 * @param *dcb    Pointer to the DCB to be initialized
 */
static void
dcb_initialize(void *dcb)
{
    *(DCB *)dcb = dcb_initialized;
}

/**
 * @brief Allocate or recycle a new DCB.
 *
 * This routine performs the generic initialisation on the DCB before returning
 * the newly allocated or recycled DCB.
 *
 * Most fields will be already initialized by the list manager, through the
 * call to list_find_free, passing the DCB initialization function.
 *
 * Remaining fields are set from the given parameters, and then the DCB is
 * flagged as ready for use.
 *
 * @param dcb_role_t    The role for the new DCB
 * @return An available DCB or NULL if none could be allocated.
 */
DCB *
dcb_alloc(dcb_role_t role, SERV_LISTENER *listener)
{
    DCB *newdcb;

    if ((newdcb = (DCB *)MXS_MALLOC(sizeof(*newdcb))) == NULL)
    {
        return NULL;
    }

    dcb_initialize(newdcb);
    newdcb->dcb_role = role;
    newdcb->listener = listener;
    newdcb->last_read = hkheartbeat;

    return newdcb;
}

/**
 * Provided only for consistency, simply calls dcb_close to guarantee
 * safe disposal of a DCB
 *
 * @param dcb   The DCB to free
 */
void
dcb_free(DCB *dcb)
{
    dcb_close(dcb);
}

/*
 * Clone a DCB for internal use, mostly used for specialist filters
 * to create dummy clients based on real clients.
 *
 * @param orig          The DCB to clone
 * @return              A DCB that can be used as a client
 */
DCB *
dcb_clone(DCB *orig)
{
    char *remote = orig->remote;

    if (remote)
    {
        remote = MXS_STRDUP(remote);
        if (!remote)
        {
            return NULL;
        }
    }

    char *user = orig->user;
    if (user)
    {
        user = MXS_STRDUP(user);
        if (!user)
        {
            MXS_FREE(remote);
            return NULL;
        }
    }

    DCB *clonedcb = dcb_alloc(orig->dcb_role, orig->listener);

    if (clonedcb)
    {
        clonedcb->fd = DCBFD_CLOSED;
        clonedcb->flags |= DCBF_CLONE;
        clonedcb->state = orig->state;
        clonedcb->data = orig->data;
        clonedcb->ssl_state = orig->ssl_state;
        clonedcb->remote = remote;
        clonedcb->user = user;
        clonedcb->thread.id = orig->thread.id;
        clonedcb->protocol = orig->protocol;

        clonedcb->func.write = dcb_null_write;
        /**
         * Close triggers closing of router session as well which is needed.
         */
        clonedcb->func.close = orig->func.close;
        clonedcb->func.auth = dcb_null_auth;
    }
    else
    {
        MXS_FREE(remote);
        MXS_FREE(user);
    }

    return clonedcb;
}

/**
 * Free a DCB and remove it from the chain of all DCBs
 *
 * NB This is called with the caller holding the zombie queue
 * spinlock
 *
 * @param dcb The DCB to free
 */
static void
dcb_final_free(DCB *dcb)
{
    CHK_DCB(dcb);
    ss_info_dassert(dcb->state == DCB_STATE_DISCONNECTED ||
                    dcb->state == DCB_STATE_ALLOC,
                    "dcb not in DCB_STATE_DISCONNECTED not in DCB_STATE_ALLOC state.");

    if (DCB_POLL_BUSY(dcb))
    {
        /* Check if DCB has outstanding poll events */
        MXS_ERROR("dcb_final_free: DCB %p has outstanding events.", dcb);
    }

    if (dcb->session)
    {
        /*<
         * Terminate client session.
         */
        MXS_SESSION *local_session = dcb->session;
        dcb->session = NULL;
        CHK_SESSION(local_session);
        if (SESSION_STATE_DUMMY != local_session->state)
        {
            bool is_client_dcb = (DCB_ROLE_CLIENT_HANDLER == dcb->dcb_role ||
                                  DCB_ROLE_INTERNAL == dcb->dcb_role);

            session_put_ref(local_session);

            if (is_client_dcb)
            {
                /** The client DCB is only freed once all other DCBs that the session
                 * uses have been freed. This will guarantee that the authentication
                 * data will be usable for all DCBs even if the client DCB has already
                 * been closed. */
                return;
            }
        }
    }
    dcb_free_all_memory(dcb);
}

/**
 * Free the memory belonging to a DCB
 *
 * NB The DCB is fully detached from all links except perhaps the session
 * dcb_client link.
 *
 * @param dcb The DCB to free
 */
void
dcb_free_all_memory(DCB *dcb)
{
    DCB_CALLBACK *cb_dcb;

    if (dcb->protocol && (!DCB_IS_CLONE(dcb)))
    {
        MXS_FREE(dcb->protocol);
    }
    if (dcb->data && dcb->authfunc.free && !DCB_IS_CLONE(dcb))
    {
        dcb->authfunc.free(dcb);
        dcb->data = NULL;
    }
    if (dcb->authfunc.destroy)
    {
        dcb->authfunc.destroy(dcb->authenticator_data);
        dcb->authenticator_data = NULL;
    }
    if (dcb->protoname)
    {
        MXS_FREE(dcb->protoname);
    }
    if (dcb->remote)
    {
        MXS_FREE(dcb->remote);
    }
    if (dcb->user)
    {
        MXS_FREE(dcb->user);
    }

    /* Clear write and read buffers */
    if (dcb->delayq)
    {
        gwbuf_free(dcb->delayq);
        dcb->delayq = NULL;
    }
    if (dcb->writeq)
    {
        gwbuf_free(dcb->writeq);
        dcb->writeq = NULL;
    }
    if (dcb->dcb_readqueue)
    {
        gwbuf_free(dcb->dcb_readqueue);
        dcb->dcb_readqueue = NULL;
    }
    if (dcb->dcb_fakequeue)
    {
        gwbuf_free(dcb->dcb_fakequeue);
        dcb->dcb_fakequeue = NULL;
    }

    while ((cb_dcb = dcb->callbacks) != NULL)
    {
        dcb->callbacks = cb_dcb->next;
        MXS_FREE(cb_dcb);
    }

    if (dcb->ssl)
    {
        SSL_free(dcb->ssl);
    }

    /* We never free the actual DCB, it is available for reuse*/
    MXS_FREE(dcb);

}

/**
 * Process the DCB zombie queue
 *
 * This routine is called by each of the polling threads with
 * the thread id of the polling thread. It must clear the bit in
 * the memdata bitmask for the polling thread that calls it. If the
 * operation of clearing this bit means that no bits are set in
 * the memdata.bitmask then the DCB is no longer able to be
 * referenced and it can be finally removed.
 *
 * @param       threadid        The thread ID of the caller
 */
void dcb_process_zombies(int threadid)
{
    if (zombies[threadid])
    {
        dcb_process_victim_queue(threadid);
    }
}

/**
 * Process the victim queue, selected from the list of zombies
 *
 * These are the DCBs that are not in use by any thread.  The corresponding
 * file descriptor is closed, the DCB marked as disconnected and the DCB
 * itself is finally freed.
 *
 * @param       listofdcb       The first victim DCB
 */
static inline void
dcb_process_victim_queue(int threadid)
{
    /** Grab the zombie queue to a local queue. This allows us to add back DCBs
     * that should not yet be closed. */
    DCB *dcblist = zombies[threadid];
    zombies[threadid] = NULL;

    while (dcblist)
    {
        DCB *dcb = dcblist;

        if (dcb->state == DCB_STATE_POLLING  || dcb->state == DCB_STATE_LISTENING)
        {
            if (dcb->state == DCB_STATE_LISTENING)
            {
                MXS_ERROR("%lu [%s] Error : Removing DCB %p but was in state %s "
                          "which is not expected for a call to dcb_close, although it"
                          "should be processed correctly. ",
                          pthread_self(),
                          __func__,
                          dcb,
                          STRDCBSTATE(dcb->state));
            }
            else
            {
                if (0 == dcb->persistentstart && dcb_maybe_add_persistent(dcb))
                {
                    /* Have taken DCB into persistent pool, no further killing */
                    dcblist = dcblist->memdata.next;
                }
                else
                {
                    /** The DCB is still polling. Shut it down and process it later. */
                    dcb_stop_polling_and_shutdown(dcb);
                    DCB *newzombie = dcblist;
                    dcblist = dcblist->memdata.next;
                    newzombie->memdata.next = zombies[threadid];
                    zombies[threadid] = newzombie;
                }

                /** Nothing to do here but to process the next DCB */
                continue;
            }
        }

        nzombies[threadid]--;

        /*
         * Into the final close logic, so if DCB is for backend server, we
         * must decrement the number of current connections.
         */
        if (DCB_ROLE_CLIENT_HANDLER == dcb->dcb_role)
        {
            if (dcb->service)
            {
                if (dcb->protocol)
                {
                    QUEUE_ENTRY conn_waiting;
                    if (mxs_dequeue(dcb->service->queued_connections, &conn_waiting))
                    {
                        DCB *waiting_dcb = (DCB *)conn_waiting.queued_object;
                        waiting_dcb->state = DCB_STATE_WAITING;
                        poll_fake_read_event(waiting_dcb);
                    }
                    else
                    {
                        atomic_add(&dcb->service->client_count, -1);
                    }
                }
            }
            else
            {
                MXS_ERROR("Closing client handler DCB, but it has no related service");
            }
        }
        if (dcb->server && 0 == dcb->persistentstart)
        {
            atomic_add(&dcb->server->stats.n_current, -1);
        }

        if (dcb->fd > 0)
        {
            /*<
             * Close file descriptor and move to clean-up phase.
             */
            if (close(dcb->fd) < 0)
            {
                int eno = errno;
                errno = 0;
                char errbuf[MXS_STRERROR_BUFLEN];
                MXS_ERROR("%lu [dcb_process_victim_queue] Error : Failed to close "
                          "socket %d on dcb %p due error %d, %s.",
                          pthread_self(),
                          dcb->fd,
                          dcb,
                          eno,
                          strerror_r(eno, errbuf, sizeof(errbuf)));
            }
            else
            {
                dcb->fd = DCBFD_CLOSED;

                MXS_DEBUG("%lu [dcb_process_victim_queue] Closed socket "
                          "%d on dcb %p.",
                          pthread_self(),
                          dcb->fd,
                          dcb);
            }
        }

        /** Move to the next DCB before freeing the previous one */
        dcblist = dcblist->memdata.next;

        /** After these calls, the DCB should be treated as if it were freed.
         * Whether it is actually freed depends on the type of the DCB and how
         * many DCBs are linked to it via the MXS_SESSION object. */
        dcb->state = DCB_STATE_DISCONNECTED;
        dcb_remove_from_list(dcb);
        dcb_final_free(dcb);
    }
}

/**
 * Remove a DCB from the poll list and trigger shutdown mechanisms.
 *
 * @param       dcb     The DCB to be processed
 */
static void
dcb_stop_polling_and_shutdown(DCB *dcb)
{
    poll_remove_dcb(dcb);
    /**
     * close protocol and router session
     */
    if (dcb->func.close != NULL)
    {
        dcb->func.close(dcb);
    }
}

/**
 * Connect to a server
 *
 * This routine will create a server connection
 * If successful the new dcb will be put in
 * epoll set by dcb->func.connect
 *
 * @param server        The server to connect to
 * @param session       The session this connection is being made for
 * @param protocol      The protocol module to use
 * @return              The new allocated dcb or NULL if the DCB was not connected
 */
DCB *
dcb_connect(SERVER *server, MXS_SESSION *session, const char *protocol)
{
    DCB         *dcb;
    MXS_PROTOCOL  *funcs;
    int         fd;
    int         rc;
    const char  *user;

    user = session_get_user(session);
    if (user && strlen(user))
    {
        MXS_DEBUG("%lu [dcb_connect] Looking for persistent connection DCB "
                  "user %s protocol %s\n", pthread_self(), user, protocol);
        dcb = server_get_persistent(server, user, session->client_dcb->remote,
                                    protocol, session->client_dcb->thread.id);
        if (dcb)
        {
            /**
             * Link dcb to session. Unlink is called in dcb_final_free
             */
            if (!session_link_dcb(session, dcb))
            {
                MXS_DEBUG("%lu [dcb_connect] Failed to link to session, the "
                          "session has been removed.\n",
                          pthread_self());
                dcb_close(dcb);
                return NULL;
            }
            MXS_DEBUG("%lu [dcb_connect] Reusing a persistent connection, dcb %p\n",
                      pthread_self(), dcb);
            dcb->persistentstart = 0;
            dcb->was_persistent = true;
            dcb->last_read = hkheartbeat;
            atomic_add_uint64(&server->stats.n_from_pool, 1);
            return dcb;
        }
        else
        {
            MXS_DEBUG("%lu [dcb_connect] Failed to find a reusable persistent connection.\n",
                      pthread_self());
        }
    }

    if ((dcb = dcb_alloc(DCB_ROLE_BACKEND_HANDLER, NULL)) == NULL)
    {
        return NULL;
    }

    if ((funcs = (MXS_PROTOCOL *)load_module(protocol,
                                             MODULE_PROTOCOL)) == NULL)
    {
        dcb->state = DCB_STATE_DISCONNECTED;
        dcb_final_free(dcb);
        MXS_ERROR("Failed to load protocol module for %s, free dcb %p\n",
                  protocol,
                  dcb);
        return NULL;
    }
    memcpy(&(dcb->func), funcs, sizeof(MXS_PROTOCOL));
    dcb->protoname = MXS_STRDUP_A(protocol);

    if (session->client_dcb->remote)
    {
        dcb->remote = MXS_STRDUP_A(session->client_dcb->remote);
    }

    const char *authenticator = server->authenticator ?
                                server->authenticator : dcb->func.auth_default ?
                                dcb->func.auth_default() : "NullAuthDeny";

    MXS_AUTHENTICATOR *authfuncs = (MXS_AUTHENTICATOR*)load_module(authenticator,
                                                               MODULE_AUTHENTICATOR);
    if (authfuncs == NULL)
    {

        MXS_ERROR("Failed to load authenticator module '%s'.", authenticator);
        dcb_close(dcb);
        return NULL;
    }

    memcpy(&dcb->authfunc, authfuncs, sizeof(MXS_AUTHENTICATOR));

    /**
     * Link dcb to session. Unlink is called in dcb_final_free
     */
    if (!session_link_dcb(session, dcb))
    {
        MXS_DEBUG("%lu [dcb_connect] Failed to link to session, the "
                  "session has been removed.",
                  pthread_self());
        dcb_final_free(dcb);
        return NULL;
    }
    fd = dcb->func.connect(dcb, server, session);

    if (fd == DCBFD_CLOSED)
    {
        MXS_DEBUG("%lu [dcb_connect] Failed to connect to server [%s]:%d, "
                  "from backend dcb %p, client dcp %p fd %d.",
                  pthread_self(),
                  server->name,
                  server->port,
                  dcb,
                  session->client_dcb,
                  session->client_dcb->fd);
        dcb->state = DCB_STATE_DISCONNECTED;
        dcb_final_free(dcb);
        return NULL;
    }
    else
    {
        MXS_DEBUG("%lu [dcb_connect] Connected to server [%s]:%d, "
                  "from backend dcb %p, client dcp %p fd %d.",
                  pthread_self(),
                  server->name,
                  server->port,
                  dcb,
                  session->client_dcb,
                  session->client_dcb->fd);
    }
    /**
     * Successfully connected to backend. Assign file descriptor to dcb
     */
    dcb->fd = fd;

    /**
     * Add server pointer to dcb
     */
    dcb->server = server;

    dcb->was_persistent = false;

    /**
     * backend_dcb is connected to backend server, and once backend_dcb
     * is added to poll set, authentication takes place as part of
     * EPOLLOUT event that will be received once the connection
     * is established.
     */

    /** Allocate DCB specific authentication data */
    if (dcb->authfunc.create &&
        (dcb->authenticator_data = dcb->authfunc.create(dcb->server->auth_instance)) == NULL)
    {
        MXS_ERROR("Failed to create authenticator for backend DCB.");
        dcb->state = DCB_STATE_DISCONNECTED;
        dcb_final_free(dcb);
        return NULL;
    }

    /**
     * Add the dcb in the poll set
     */
    rc = poll_add_dcb(dcb);

    if (rc)
    {
        dcb->state = DCB_STATE_DISCONNECTED;
        dcb_final_free(dcb);
        return NULL;
    }
    /**
     * The dcb will be addded into poll set by dcb->func.connect
     */
    atomic_add(&server->stats.n_connections, 1);
    atomic_add(&server->stats.n_current, 1);

    return dcb;
}

/**
 * General purpose read routine to read data from a socket in the
 * Descriptor Control Block and append it to a linked list of buffers.
 * The list may be empty, in which case *head == NULL. The third
 * parameter indicates the maximum number of bytes to be read (needed
 * for SSL processing) with 0 meaning no limit.
 *
 * @param dcb       The DCB to read from
 * @param head      Pointer to linked list to append data to
 * @param maxbytes  Maximum bytes to read (0 = no limit)
 * @return          -1 on error, otherwise the total number of bytes read
 */
int dcb_read(DCB   *dcb,
             GWBUF **head,
             int maxbytes)
{
    int     nsingleread = 0;
    int     nreadtotal = 0;

    if (dcb->dcb_readqueue)
    {
        *head = gwbuf_append(*head, dcb->dcb_readqueue);
        dcb->dcb_readqueue = NULL;
        nreadtotal = gwbuf_length(*head);
    }
    else if (dcb->dcb_fakequeue)
    {
        *head = gwbuf_append(*head, dcb->dcb_fakequeue);
        dcb->dcb_fakequeue = NULL;
        nreadtotal = gwbuf_length(*head);
    }

    if (SSL_HANDSHAKE_DONE == dcb->ssl_state || SSL_ESTABLISHED == dcb->ssl_state)
    {
        return dcb_read_SSL(dcb, head);
    }

    CHK_DCB(dcb);

    if (dcb->fd <= 0)
    {
        /* <editor-fold defaultstate="collapsed" desc=" Error Logging "> */
        MXS_ERROR("%lu [dcb_read] Error : Read failed, dcb is %s.",
                  pthread_self(),
                  dcb->fd == DCBFD_CLOSED ? "closed" : "cloned, not readable");
        /* </editor-fold> */
        return 0;
    }

    while (0 == maxbytes || nreadtotal < maxbytes)
    {
        int bytes_available;

        bytes_available = dcb_bytes_readable(dcb);
        if (bytes_available <= 0)
        {
            return bytes_available < 0 ? -1 :
                   /** Handle closed client socket */
                   dcb_read_no_bytes_available(dcb, nreadtotal);
        }
        else
        {
            GWBUF *buffer;
            dcb->last_read = hkheartbeat;

            buffer = dcb_basic_read(dcb, bytes_available, maxbytes, nreadtotal, &nsingleread);
            if (buffer)
            {
                nreadtotal += nsingleread;
                /* <editor-fold defaultstate="collapsed" desc=" Debug Logging "> */
                MXS_DEBUG("%lu [dcb_read] Read %d bytes from dcb %p in state %s "
                          "fd %d.",
                          pthread_self(),
                          nsingleread,
                          dcb,
                          STRDCBSTATE(dcb->state),
                          dcb->fd);
                /* </editor-fold> */
                /*< Assign the target server for the gwbuf */
                buffer->server = dcb->server;
                /*< Append read data to the gwbuf */
                *head = gwbuf_append(*head, buffer);
            }
            else
            {
                break;
            }
        }
    } /*< while (0 == maxbytes || nreadtotal < maxbytes) */

    return nreadtotal;
}

/**
 * Find the number of bytes available for the DCB's socket
 *
 * @param dcb       The DCB to read from
 * @return          -1 on error, otherwise the total number of bytes available
 */
static int
dcb_bytes_readable(DCB *dcb)
{
    int bytesavailable;

    if (-1 == ioctl(dcb->fd, FIONREAD, &bytesavailable))
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        /* <editor-fold defaultstate="collapsed" desc=" Error Logging "> */
        MXS_ERROR("%lu [dcb_read] Error : ioctl FIONREAD for dcb %p in "
                  "state %s fd %d failed due error %d, %s.",
                  pthread_self(),
                  dcb,
                  STRDCBSTATE(dcb->state),
                  dcb->fd,
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        /* </editor-fold> */
        return -1;
    }
    else
    {
        return bytesavailable;
    }
}

/**
 * Determine the return code needed when read has run out of data
 *
 * @param dcb           The DCB to read from
 * @param nreadtotal    Number of bytes that have been read
 * @return              -1 on error, 0 for conditions not treated as error
 */
static int
dcb_read_no_bytes_available(DCB *dcb, int nreadtotal)
{
    /** Handle closed client socket */
    if (nreadtotal == 0 && DCB_ROLE_CLIENT_HANDLER == dcb->dcb_role)
    {
        char c;
        int l_errno = 0;
        long r = -1;

        /* try to read 1 byte, without consuming the socket buffer */
        r = recv(dcb->fd, &c, sizeof(char), MSG_PEEK);
        l_errno = errno;

        if (r <= 0 &&
            l_errno != EAGAIN &&
            l_errno != EWOULDBLOCK &&
            l_errno != 0)
        {
            return -1;
        }
    }
    return nreadtotal;
}

/**
 * Basic read function to carry out a single read operation on the DCB socket.
 *
 * @param dcb               The DCB to read from
 * @param bytesavailable    Pointer to linked list to append data to
 * @param maxbytes          Maximum bytes to read (0 = no limit)
 * @param nreadtotal        Total number of bytes already read
 * @param nsingleread       To be set as the number of bytes read this time
 * @return                  GWBUF* buffer containing new data, or null.
 */
static GWBUF *
dcb_basic_read(DCB *dcb, int bytesavailable, int maxbytes, int nreadtotal, int *nsingleread)
{
    GWBUF *buffer;

    int bufsize = MXS_MIN(bytesavailable, MXS_MAX_NW_READ_BUFFER_SIZE);
    if (maxbytes)
    {
        bufsize = MXS_MIN(bufsize, maxbytes - nreadtotal);
    }

    if ((buffer = gwbuf_alloc(bufsize)) == NULL)
    {
        /*<
         * This is a fatal error which should cause shutdown.
         * Todo shutdown if memory allocation fails.
         */
        char errbuf[MXS_STRERROR_BUFLEN];
        /* <editor-fold defaultstate="collapsed" desc=" Error Logging "> */
        MXS_ERROR("%lu [dcb_read] Error : Failed to allocate read buffer "
                  "for dcb %p fd %d, due %d, %s.",
                  pthread_self(),
                  dcb,
                  dcb->fd,
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        /* </editor-fold> */
        *nsingleread = -1;
    }
    else
    {
        *nsingleread = read(dcb->fd, GWBUF_DATA(buffer), bufsize);
        dcb->stats.n_reads++;

        if (*nsingleread <= 0)
        {
            if (errno != 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                char errbuf[MXS_STRERROR_BUFLEN];
                /* <editor-fold defaultstate="collapsed" desc=" Error Logging "> */
                MXS_ERROR("%lu [dcb_read] Error : Read failed, dcb %p in state "
                          "%s fd %d, due %d, %s.",
                          pthread_self(),
                          dcb,
                          STRDCBSTATE(dcb->state),
                          dcb->fd,
                          errno,
                          strerror_r(errno, errbuf, sizeof(errbuf)));
                /* </editor-fold> */
            }
            gwbuf_free(buffer);
            buffer = NULL;
        }
    }
    return buffer;
}

/**
 * General purpose read routine to read data from a socket through the SSL
 * structure lined with this DCB and append it to a linked list of buffers.
 * The list may be empty, in which case *head == NULL. The SSL structure should
 * be initialized and the SSL handshake should be done.
 *
 * @param dcb   The DCB to read from
 * @param head  Pointer to linked list to append data to
 * @return      -1 on error, otherwise the total number of bytes read
 */
static int
dcb_read_SSL(DCB *dcb, GWBUF **head)
{
    GWBUF *buffer;
    int nsingleread = 0, nreadtotal = 0;
    int start_length = gwbuf_length(*head);

    CHK_DCB(dcb);

    if (dcb->fd <= 0)
    {
        MXS_ERROR("Read failed, dcb is %s.",
                  dcb->fd == DCBFD_CLOSED ? "closed" : "cloned, not readable");
        return -1;
    }

    if (dcb->ssl_write_want_read)
    {
        dcb_drain_writeq(dcb);
    }

    dcb->last_read = hkheartbeat;
    buffer = dcb_basic_read_SSL(dcb, &nsingleread);
    if (buffer)
    {
        nreadtotal += nsingleread;
        *head = gwbuf_append(*head, buffer);

        while (buffer)
        {
            dcb->last_read = hkheartbeat;
            buffer = dcb_basic_read_SSL(dcb, &nsingleread);
            if (buffer)
            {
                nreadtotal += nsingleread;
                /*< Append read data to the gwbuf */
                *head = gwbuf_append(*head, buffer);
            }
        }
    }

    ss_dassert(gwbuf_length(*head) == (start_length + nreadtotal));

    return nsingleread < 0 ? nsingleread : nreadtotal;
}

/**
 * Basic read function to carry out a single read on the DCB's SSL connection
 *
 * @param dcb           The DCB to read from
 * @param nsingleread   To be set as the number of bytes read this time
 * @return              GWBUF* buffer containing the data, or null.
 */
static GWBUF *
dcb_basic_read_SSL(DCB *dcb, int *nsingleread)
{
    unsigned char temp_buffer[MXS_MAX_NW_READ_BUFFER_SIZE];
    GWBUF *buffer = NULL;

    *nsingleread = SSL_read(dcb->ssl, (void *)temp_buffer, MXS_MAX_NW_READ_BUFFER_SIZE);
    dcb->stats.n_reads++;

    switch (SSL_get_error(dcb->ssl, *nsingleread))
    {
    case SSL_ERROR_NONE:
        /* Successful read */
        MXS_DEBUG("%lu [%s] Read %d bytes from dcb %p in state %s "
                  "fd %d.",
                  pthread_self(),
                  __func__,
                  *nsingleread,
                  dcb,
                  STRDCBSTATE(dcb->state),
                  dcb->fd);
        if (*nsingleread && (buffer = gwbuf_alloc_and_load(*nsingleread, (void *)temp_buffer)) == NULL)
        {
            /*<
             * This is a fatal error which should cause shutdown.
             * Todo shutdown if memory allocation fails.
             */
            char errbuf[MXS_STRERROR_BUFLEN];
            /* <editor-fold defaultstate="collapsed" desc=" Error Logging "> */
            MXS_ERROR("%lu [dcb_read] Error : Failed to allocate read buffer "
                      "for dcb %p fd %d, due %d, %s.",
                      pthread_self(),
                      dcb,
                      dcb->fd,
                      errno,
                      strerror_r(errno, errbuf, sizeof(errbuf)));
            /* </editor-fold> */
            *nsingleread = -1;
            return NULL;
        }

        /* If we were in a retry situation, need to clear flag and attempt write */
        if (dcb->ssl_read_want_write || dcb->ssl_read_want_read)
        {
            dcb->ssl_read_want_write = false;
            dcb->ssl_read_want_read = false;
            dcb_drain_writeq(dcb);
        }
        break;

    case SSL_ERROR_ZERO_RETURN:
        /* react to the SSL connection being closed */
        MXS_DEBUG("%lu [%s] SSL connection appears to have hung up",
                  pthread_self(),
                  __func__
                 );
        poll_fake_hangup_event(dcb);
        *nsingleread = 0;
        break;

    case SSL_ERROR_WANT_READ:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        MXS_DEBUG("%lu [%s] SSL connection want read",
                  pthread_self(),
                  __func__
                 );
        dcb->ssl_read_want_write = false;
        dcb->ssl_read_want_read = true;
        *nsingleread = 0;
        break;

    case SSL_ERROR_WANT_WRITE:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        MXS_DEBUG("%lu [%s] SSL connection want write",
                  pthread_self(),
                  __func__
                 );
        dcb->ssl_read_want_write = true;
        dcb->ssl_read_want_read = false;
        *nsingleread = 0;
        break;

    case SSL_ERROR_SYSCALL:
        *nsingleread = dcb_log_errors_SSL(dcb, __func__, *nsingleread);
        break;

    default:
        *nsingleread = dcb_log_errors_SSL(dcb, __func__, *nsingleread);
        break;
    }
    return buffer;
}

/**
 * Log errors from an SSL operation
 *
 * @param dcb       The DCB of the client
 * @param called_by Name of the calling function
 * @param ret       Return code from SSL operation if error is SSL_ERROR_SYSCALL
 * @return          -1 if an error found, 0 if no error found
 */
static int
dcb_log_errors_SSL (DCB *dcb, const char *called_by, int ret)
{
    char errbuf[MXS_STRERROR_BUFLEN];
    unsigned long ssl_errno;

    ssl_errno = ERR_get_error();
    if (0 == ssl_errno)
    {
        return 0;
    }
    if (ret || ssl_errno)
    {
        MXS_ERROR("SSL operation failed in %s, dcb %p in state "
                  "%s fd %d return code %d. More details may follow.",
                  called_by,
                  dcb,
                  STRDCBSTATE(dcb->state),
                  dcb->fd,
                  ret);
    }
    if (ret && !ssl_errno)
    {
        int local_errno = errno;
        MXS_ERROR("SSL error caused by TCP error %d %s",
                  local_errno,
                  strerror_r(local_errno, errbuf, sizeof(errbuf))
                 );
    }
    else
    {
        while (ssl_errno != 0)
        {
            ERR_error_string_n(ssl_errno, errbuf, MXS_STRERROR_BUFLEN);
            MXS_ERROR("%s", errbuf);
            ssl_errno = ERR_get_error();
        }
    }
    return -1;
}

/**
 * General purpose routine to write to a DCB
 *
 * @param dcb   The DCB of the client
 * @param queue Queue of buffers to write
 * @return      0 on failure, 1 on success
 */
int
dcb_write(DCB *dcb, GWBUF *queue)
{
    bool empty_queue;
    bool below_water;

    below_water = (dcb->high_water && dcb->writeqlen < dcb->high_water);
    // The following guarantees that queue is not NULL
    if (!dcb_write_parameter_check(dcb, queue))
    {
        return 0;
    }

    empty_queue = (dcb->writeq == NULL);
    /*
     * Add our data to the write queue.  If the queue already had data,
     * then there will be an EPOLLOUT event to drain what is already queued.
     * If it did not already have data, we call the drain write queue
     * function immediately to attempt to write the data.
     */
    dcb->writeqlen += gwbuf_length(queue);
    dcb->writeq = gwbuf_append(dcb->writeq, queue);
    dcb->stats.n_buffered++;

    MXS_DEBUG("%lu [dcb_write] Append to writequeue. %d writes "
              "buffered for dcb %p in state %s fd %d",
              pthread_self(),
              dcb->stats.n_buffered,
              dcb,
              STRDCBSTATE(dcb->state),
              dcb->fd);
    if (empty_queue)
    {
        dcb_drain_writeq(dcb);
    }
    dcb_write_tidy_up(dcb, below_water);

    return 1;
}

/**
 * Check the parameters for dcb_write
 *
 * @param dcb   The DCB of the client
 * @param queue Queue of buffers to write
 * @return true if parameters acceptable, false otherwise
 */
static inline bool
dcb_write_parameter_check(DCB *dcb, GWBUF *queue)
{
    if (queue == NULL)
    {
        return false;
    }

    if (dcb->fd <= 0)
    {
        MXS_ERROR("Write failed, dcb is %s.",
                  dcb->fd == DCBFD_CLOSED ? "closed" : "cloned, not writable");
        gwbuf_free(queue);
        return false;
    }

    if (dcb->session == NULL || dcb->session->state != SESSION_STATE_STOPPING)
    {
        /**
         * SESSION_STATE_STOPPING means that one of the backends is closing
         * the router session. Some backends may have not completed
         * authentication yet and thus they have no information about router
         * being closed. Session state is changed to SESSION_STATE_STOPPING
         * before router's closeSession is called and that tells that DCB may
         * still be writable.
         */
        if (dcb->state != DCB_STATE_ALLOC &&
            dcb->state != DCB_STATE_POLLING &&
            dcb->state != DCB_STATE_LISTENING &&
            dcb->state != DCB_STATE_NOPOLLING)
        {
            MXS_DEBUG("%lu [dcb_write] Write aborted to dcb %p because "
                      "it is in state %s",
                      pthread_self(),
                      dcb,
                      STRDCBSTATE(dcb->state));
            gwbuf_free(queue);
            return false;
        }
    }
    return true;
}

/**
 * Debug log write failure, except when it is COM_QUIT
 *
 * @param dcb   The DCB of the client
 * @param queue Queue of buffers to write
 * @param eno   Error number for logging
 */
static void
dcb_log_write_failure(DCB *dcb, GWBUF *queue, int eno)
{
    if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_DEBUG))
    {
        if (eno == EPIPE)
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_DEBUG("%lu [dcb_write] Write to dcb "
                      "%p in state %s fd %d failed "
                      "due errno %d, %s",
                      pthread_self(),
                      dcb,
                      STRDCBSTATE(dcb->state),
                      dcb->fd,
                      eno,
                      strerror_r(eno, errbuf, sizeof(errbuf)));
        }
    }

    if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_ERR))
    {
        if (eno != EPIPE &&
            eno != EAGAIN &&
            eno != EWOULDBLOCK)
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_ERROR("Write to dcb %p in "
                      "state %s fd %d failed due "
                      "errno %d, %s",
                      dcb,
                      STRDCBSTATE(dcb->state),
                      dcb->fd,
                      eno,
                      strerror_r(eno, errbuf, sizeof(errbuf)));

        }

    }

    bool dolog = true;

    if (eno != 0           &&
        eno != EAGAIN      &&
        eno != EWOULDBLOCK)
    {
        /**
         * Do not log if writing COM_QUIT to backend failed.
         */
        if (GWBUF_IS_TYPE_MYSQL(queue))
        {
            uint8_t* data = GWBUF_DATA(queue);

            if (data[4] == 0x01)
            {
                dolog = false;
            }
        }
        if (dolog)
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_DEBUG("%lu [dcb_write] Writing to %s socket failed due %d, %s.",
                      pthread_self(),
                      DCB_ROLE_CLIENT_HANDLER == dcb->dcb_role ? "client" : "backend server",
                      eno,
                      strerror_r(eno, errbuf, sizeof(errbuf)));
        }
    }
}

/**
 * Last few things to do at end of a write
 *
 * @param dcb           The DCB of the client
 * @param below_water   A boolean
 */
static inline void
dcb_write_tidy_up(DCB *dcb, bool below_water)
{
    if (dcb->high_water && dcb->writeqlen > dcb->high_water && below_water)
    {
        atomic_add(&dcb->stats.n_high_water, 1);
        dcb_call_callback(dcb, DCB_REASON_HIGH_WATER);
    }
}

/**
 * Drain the write queue of a DCB. This is called as part of the EPOLLOUT handling
 * of a socket and will try to send any buffered data from the write queue
 * up until the point the write would block.
 *
 * @param dcb   DCB to drain the write queue of
 * @return The number of bytes written
 */
int
dcb_drain_writeq(DCB *dcb)
{
    int total_written = 0;
    GWBUF *local_writeq;
    bool above_water;
    /*
     * Loop over the buffer chain in the pending writeq
     * Send as much of the data in that chain as possible and
     * leave any balance on the write queue.
     *
     * Note that dcb_grab_writeq will set a flag (dcb->draining_flag) to prevent
     * this function being entered a second time (by another thread) while
     * processing is continuing. If the flag is already set, the return from
     * dcb_grab_writeq will be NULL and so the outer while loop will not
     * execute. The value of total_written will therefore remain zero and
     * the nothing will happen in the wrap up code.
     *
     * @note The callback DCB_REASON_DRAINED is misleading. It is triggered
     * pretty much every time there is an EPOLLOUT event and also when a
     * write occurs while draining is still in progress. It is used only in
     * the binlog router, which cannot function without the callback. The
     * callback does not mean that a non-empty queue has been drained, or even
     * that the queue is presently empty.
     */
    local_writeq = dcb_grab_writeq(dcb, true);
    if (NULL == local_writeq)
    {
        dcb_call_callback(dcb, DCB_REASON_DRAINED);
        return 0;
    }
    above_water = (dcb->low_water && gwbuf_length(local_writeq) > dcb->low_water);
    do
    {
        /*
         * Process the list of buffers taken from dcb->writeq
         */
        while (local_writeq != NULL)
        {
            bool stop_writing = false;
            int written;
            /* The value put into written will be >= 0 */
            if (dcb->ssl)
            {
                written = gw_write_SSL(dcb, local_writeq, &stop_writing);
            }
            else
            {
                written = gw_write(dcb, local_writeq, &stop_writing);
            }
            /*
             * If the stop_writing boolean is set, writing has become blocked,
             * so the remaining data is put back at the front of the write
             * queue.
             *
             * However, if we have been called while processing the queue, it
             * is possible that writing has blocked and then become unblocked.
             * So an attempt is made to put the write queue into the local list
             * and loop again.
             */
            if (stop_writing)
            {
                dcb->writeq = gwbuf_append(local_writeq, dcb->writeq);

                if (dcb->drain_called_while_busy)
                {
                    local_writeq = dcb->writeq;
                    dcb->writeq = NULL;
                    dcb->drain_called_while_busy = false;
                    continue;
                }
                else
                {
                    dcb->draining_flag = false;
                    goto wrap_up;
                }
            }
            /*
             * Consume the bytes we have written from the list of buffers,
             * and increment the total bytes written.
             */
            local_writeq = gwbuf_consume(local_writeq, written);
            total_written += written;
        }
    }
    while ((local_writeq = dcb_grab_writeq(dcb, false)) != NULL);
    /* The write queue has drained, potentially need to call a callback function */
    dcb_call_callback(dcb, DCB_REASON_DRAINED);

wrap_up:

    /*
     * If nothing has been written, the callback events cannot have occurred
     * and there is no need to adjust the length of the write queue.
     */
    if (total_written)
    {
        dcb->writeqlen -= total_written;

        /* Check if the draining has taken us from above water to below water */
        if (above_water && dcb->writeqlen < dcb->low_water)
        {
            atomic_add(&dcb->stats.n_low_water, 1);
            dcb_call_callback(dcb, DCB_REASON_LOW_WATER);
        }

    }
    return total_written;
}

/**
 * @brief If draining is not already under way, extracts the write queue
 *
 * Since we are intending to manipulate the write queue (a linked list) and
 * possibly adjust some DCB flags, a spinlock is required. If we are already
 * draining the queue, the flag is set to indicate a call while draining and
 * null return is made.
 *
 * Otherwise, the DCB write queue is transferred into a local variable which
 * will be returned to the caller, and the pointer in the DCB set to NULL.
 * If the list to be returned is empty, we are stopping draining, otherwise
 * we are engaged in draining.
 *
 * @param dcb Request handler DCB whose write queue is being drained
 * @param first_time Set to true only on the first call in dcb_drain_writeq
 * @return A local list of buffers taken from the DCB write queue
 */
static GWBUF *
dcb_grab_writeq(DCB *dcb, bool first_time)
{
    GWBUF *local_writeq = NULL;

    if (first_time && dcb->ssl_read_want_write)
    {
        poll_fake_read_event(dcb);
    }

    if (first_time && dcb->draining_flag)
    {
        dcb->drain_called_while_busy = true;
    }
    else
    {
        local_writeq = dcb->writeq;
        dcb->draining_flag = local_writeq ? true : false;
        dcb->writeq = NULL;
    }

    return local_writeq;
}

static void log_illegal_dcb(DCB *dcb)
{
    const char *connected_to;

    switch (dcb->dcb_role)
    {
    case DCB_ROLE_BACKEND_HANDLER:
        connected_to = dcb->server->unique_name;
        break;

    case DCB_ROLE_CLIENT_HANDLER:
        connected_to = dcb->remote;
        break;

    case DCB_ROLE_INTERNAL:
        connected_to = "Internal DCB";
        break;

    case DCB_ROLE_SERVICE_LISTENER:
        connected_to = dcb->service->name;
        break;

    default:
        connected_to = "Illegal DCB role";
        break;
    }

    MXS_ERROR("[dcb_close] Error : Removing DCB %p but it is in state %s "
              "which is not legal for a call to dcb_close. The DCB is connected to: %s",
              dcb, STRDCBSTATE(dcb->state), connected_to);
}

/**
 * Removes dcb from poll set, and adds it to zombies list. As a consequence,
 * dcb first moves to DCB_STATE_NOPOLLING, and then to DCB_STATE_ZOMBIE state.
 * At the end of the function state may not be DCB_STATE_ZOMBIE because once
 * dcb_initlock is released parallel threads may change the state.
 *
 * Parameters:
 * @param dcb The DCB to close
 *
 *
 */
void
dcb_close(DCB *dcb)
{
    CHK_DCB(dcb);

    if (DCB_STATE_UNDEFINED == dcb->state
        || DCB_STATE_DISCONNECTED == dcb->state)
    {
        log_illegal_dcb(dcb);
        raise(SIGABRT);
    }

    /**
     * dcb_close may be called for freshly created dcb, in which case
     * it only needs to be freed.
     */
    if (dcb->state == DCB_STATE_ALLOC && dcb->fd == DCBFD_CLOSED)
    {
        dcb_final_free(dcb);
    }
    /*
     * If DCB is in persistent pool, mark it as an error and exit
     */
    else if (dcb->persistentstart > 0)
    {
        dcb->dcb_errhandle_called = true;
    }
    else if (!dcb->dcb_is_zombie)
    {
        if (DCB_ROLE_BACKEND_HANDLER == dcb->dcb_role && 0 == dcb->persistentstart
            && dcb->server && DCB_STATE_POLLING == dcb->state)
        {
            /* May be a candidate for persistence, so save user name */
            const char *user;
            user = session_get_user(dcb->session);
            if (user && strlen(user) && !dcb->user)
            {
                dcb->user = MXS_STRDUP_A(user);
            }
        }
        /*<
         * Add closing dcb to the top of the list, setting zombie marker
         */
        int owner = dcb->thread.id;
        dcb->dcb_is_zombie = true;
        dcb->memdata.next = zombies[owner];
        zombies[owner] = dcb;
        nzombies[owner]++;
        if (nzombies[owner] > maxzombies)
        {
            maxzombies = nzombies[owner];
        }
    }
    else
    {
        /** DCBs in the zombie queue can still receive events which means that
         * a DCB can be closed multiple times while it's in the zombie queue. */
    }
}

/**
 * Add DCB to persistent pool if it qualifies, close otherwise
 *
 * @param dcb   The DCB to go to persistent pool or be closed
 * @return      bool - whether the DCB was added to the pool
 *
 */
static bool
dcb_maybe_add_persistent(DCB *dcb)
{
    if (dcb->user != NULL
        && (dcb->func.established == NULL || dcb->func.established(dcb))
        && strlen(dcb->user)
        && dcb->server
        && dcb->session
        && session_valid_for_pool(dcb->session)
        && dcb->server->persistpoolmax
        && (dcb->server->status & SERVER_RUNNING)
        && !dcb->dcb_errhandle_called
        && !(dcb->flags & DCBF_HUNG)
        && dcb_persistent_clean_count(dcb, dcb->thread.id, false) < dcb->server->persistpoolmax
        && dcb->server->stats.n_persistent < dcb->server->persistpoolmax)
    {
        DCB_CALLBACK *loopcallback;
        MXS_DEBUG("%lu [dcb_maybe_add_persistent] Adding DCB to persistent pool, user %s.\n",
                  pthread_self(),
                  dcb->user);
        dcb->was_persistent = false;
        dcb->dcb_is_zombie = false;
        dcb->persistentstart = time(NULL);
        if (dcb->session)
            /*<
             * Terminate client session.
             */
        {
            MXS_SESSION *local_session = dcb->session;
            session_set_dummy(dcb);
            CHK_SESSION(local_session);
            if (SESSION_STATE_DUMMY != local_session->state)
            {
                session_put_ref(local_session);
            }
        }

        while ((loopcallback = dcb->callbacks) != NULL)
        {
            dcb->callbacks = loopcallback->next;
            MXS_FREE(loopcallback);
        }

        /** Free all buffered data */
        gwbuf_free(dcb->dcb_fakequeue);
        gwbuf_free(dcb->dcb_readqueue);
        gwbuf_free(dcb->delayq);
        gwbuf_free(dcb->writeq);
        dcb->dcb_fakequeue = NULL;
        dcb->dcb_readqueue = NULL;
        dcb->delayq = NULL;
        dcb->writeq = NULL;

        dcb->nextpersistent = dcb->server->persistent[dcb->thread.id];
        dcb->server->persistent[dcb->thread.id] = dcb;
        atomic_add(&dcb->server->stats.n_persistent, 1);
        atomic_add(&dcb->server->stats.n_current, -1);
        return true;
    }
    else if (dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER && dcb->server)
    {
        MXS_DEBUG("%lu [dcb_maybe_add_persistent] Not adding DCB %p to persistent pool, "
                  "user %s, max for pool %ld, error handle called %s, hung flag %s, "
                  "server status %d, pool count %d.\n",
                  pthread_self(),
                  dcb,
                  dcb->user ? dcb->user : "",
                  dcb->server->persistpoolmax,
                  dcb->dcb_errhandle_called ? "true" : "false",
                  (dcb->flags & DCBF_HUNG) ? "true" : "false",
                  dcb->server->status,
                  dcb->server->stats.n_persistent);
    }
    return false;
}

/**
 * Diagnostic to print a DCB
 *
 * @param dcb   The DCB to print
 *
 */
void
printDCB(DCB *dcb)
{
    printf("DCB: %p\n", (void *)dcb);
    printf("\tDCB state:            %s\n", gw_dcb_state2string(dcb->state));
    if (dcb->remote)
    {
        printf("\tConnected to:         %s\n", dcb->remote);
    }
    if (dcb->user)
    {
        printf("\tUsername:             %s\n", dcb->user);
    }
    if (dcb->protoname)
    {
        printf("\tProtocol:             %s\n", dcb->protoname);
    }
    if (dcb->writeq)
    {
        printf("\tQueued write data:    %u\n", gwbuf_length(dcb->writeq));
    }
    char *statusname = server_status(dcb->server);
    if (statusname)
    {
        printf("\tServer status:            %s\n", statusname);
        MXS_FREE(statusname);
    }
    char *rolename = dcb_role_name(dcb);
    if (rolename)
    {
        printf("\tRole:                     %s\n", rolename);
        MXS_FREE(rolename);
    }
    printf("\tStatistics:\n");
    printf("\t\tNo. of Reads:                       %d\n",
           dcb->stats.n_reads);
    printf("\t\tNo. of Writes:                      %d\n",
           dcb->stats.n_writes);
    printf("\t\tNo. of Buffered Writes:             %d\n",
           dcb->stats.n_buffered);
    printf("\t\tNo. of Accepts:                     %d\n",
           dcb->stats.n_accepts);
    printf("\t\tNo. of High Water Events:   %d\n",
           dcb->stats.n_high_water);
    printf("\t\tNo. of Low Water Events:    %d\n",
           dcb->stats.n_low_water);
}
/**
 * Display an entry from the spinlock statistics data
 *
 * @param       dcb     The DCB to print to
 * @param       desc    Description of the statistic
 * @param       value   The statistic value
 */
static void
spin_reporter(void *dcb, char *desc, int value)
{
    dcb_printf((DCB *)dcb, "\t\t%-40s  %d\n", desc, value);
}

bool printAllDCBs_cb(DCB *dcb, void *data)
{
    printDCB(dcb);
    return true;
}

/**
 * Diagnostic to print all DCB allocated in the system
 *
 */
void printAllDCBs()
{
    dcb_foreach(printAllDCBs_cb, NULL);
}

/**
 * Diagnostic to print one DCB in the system
 *
 * @param       pdcb    DCB to print results to
 * @param       dcb     DCB to be printed
 */
void
dprintOneDCB(DCB *pdcb, DCB *dcb)
{
    dcb_printf(pdcb, "DCB: %p\n", (void *)dcb);
    dcb_printf(pdcb, "\tDCB state:          %s\n",
               gw_dcb_state2string(dcb->state));
    if (dcb->session && dcb->session->service)
    {
        dcb_printf(pdcb, "\tService:            %s\n",
                   dcb->session->service->name);
    }
    if (dcb->remote)
    {
        dcb_printf(pdcb, "\tConnected to:       %s\n",
                   dcb->remote);
    }
    if (dcb->server)
    {
        if (dcb->server->name)
        {
            dcb_printf(pdcb, "\tServer name/IP:     %s\n",
                       dcb->server->name);
        }
        if (dcb->server->port)
        {
            dcb_printf(pdcb, "\tPort number:        %d\n",
                       dcb->server->port);
        }
    }
    if (dcb->user)
    {
        dcb_printf(pdcb, "\tUsername:           %s\n",
                   dcb->user);
    }
    if (dcb->protoname)
    {
        dcb_printf(pdcb, "\tProtocol:           %s\n",
                   dcb->protoname);
    }
    if (dcb->writeq)
    {
        dcb_printf(pdcb, "\tQueued write data:  %d\n",
                   gwbuf_length(dcb->writeq));
    }
    char *statusname = server_status(dcb->server);
    if (statusname)
    {
        dcb_printf(pdcb, "\tServer status:            %s\n", statusname);
        MXS_FREE(statusname);
    }
    char *rolename = dcb_role_name(dcb);
    if (rolename)
    {
        dcb_printf(pdcb, "\tRole:                     %s\n", rolename);
        MXS_FREE(rolename);
    }
    dcb_printf(pdcb, "\tStatistics:\n");
    dcb_printf(pdcb, "\t\tNo. of Reads:             %d\n", dcb->stats.n_reads);
    dcb_printf(pdcb, "\t\tNo. of Writes:            %d\n", dcb->stats.n_writes);
    dcb_printf(pdcb, "\t\tNo. of Buffered Writes:   %d\n", dcb->stats.n_buffered);
    dcb_printf(pdcb, "\t\tNo. of Accepts:           %d\n", dcb->stats.n_accepts);
    dcb_printf(pdcb, "\t\tNo. of High Water Events: %d\n", dcb->stats.n_high_water);
    dcb_printf(pdcb, "\t\tNo. of Low Water Events:  %d\n", dcb->stats.n_low_water);
    if (dcb->flags & DCBF_CLONE)
    {
        dcb_printf(pdcb, "\t\tDCB is a clone.\n");
    }
    if (dcb->persistentstart)
    {
        char buff[20];
        struct tm timeinfo;
        localtime_r(&dcb->persistentstart, &timeinfo);
        strftime(buff, sizeof(buff), "%b %d %H:%M:%S", &timeinfo);
        dcb_printf(pdcb, "\t\tAdded to persistent pool:       %s\n", buff);
    }
}

static bool dprint_all_dcbs_cb(DCB *dcb, void *data)
{
    DCB *pdcb = (DCB*)data;
    dprintOneDCB(pdcb, dcb);
    return true;
}

/**
 * Diagnostic to print all DCB allocated in the system
 *
 * @param       pdcb    DCB to print results to
 */
void dprintAllDCBs(DCB *pdcb)
{
    dcb_foreach(dprint_all_dcbs_cb, pdcb);
}

static bool dlist_dcbs_cb(DCB *dcb, void *data)
{
    DCB *pdcb = (DCB*)data;
    dcb_printf(pdcb, " %-16p | %-26s | %-18s | %s\n",
               dcb, gw_dcb_state2string(dcb->state),
               ((dcb->session && dcb->session->service) ? dcb->session->service->name : ""),
               (dcb->remote ? dcb->remote : ""));
    return true;
}

/**
 * Diagnostic routine to print DCB data in a tabular form.
 *
 * @param       pdcb    DCB to print results to
 */
void
dListDCBs(DCB *pdcb)
{
    dcb_printf(pdcb, "Descriptor Control Blocks\n");
    dcb_printf(pdcb, "------------------+----------------------------+--------------------+----------\n");
    dcb_printf(pdcb, " %-16s | %-26s | %-18s | %s\n",
               "DCB", "State", "Service", "Remote");
    dcb_printf(pdcb, "------------------+----------------------------+--------------------+----------\n");
    dcb_foreach(dlist_dcbs_cb, pdcb);
    dcb_printf(pdcb, "------------------+----------------------------+--------------------+----------\n\n");
}

static bool dlist_clients_cb(DCB *dcb, void *data)
{
    DCB *pdcb = (DCB*)data;

    if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER)
    {
        dcb_printf(pdcb, " %-15s | %16p | %-20s | %10p\n",
                   (dcb->remote ? dcb->remote : ""),
                   dcb, (dcb->session->service ?
                         dcb->session->service->name : ""),
                   dcb->session);
    }

    return true;
}

/**
 * Diagnostic routine to print client DCB data in a tabular form.
 *
 * @param       pdcb    DCB to print results to
 */
void
dListClients(DCB *pdcb)
{
    dcb_printf(pdcb, "Client Connections\n");
    dcb_printf(pdcb, "-----------------+------------------+----------------------+------------\n");
    dcb_printf(pdcb, " %-15s | %-16s | %-20s | %s\n",
               "Client", "DCB", "Service", "Session");
    dcb_printf(pdcb, "-----------------+------------------+----------------------+------------\n");
    dcb_foreach(dlist_clients_cb, pdcb);
    dcb_printf(pdcb, "-----------------+------------------+----------------------+------------\n\n");
}

/**
 * Diagnostic to print a DCB to another DCB
 *
 * @param pdcb  The DCB to which send the output
 * @param dcb   The DCB to print
 */
void
dprintDCB(DCB *pdcb, DCB *dcb)
{
    dcb_printf(pdcb, "DCB: %p\n", (void *)dcb);
    dcb_printf(pdcb, "\tDCB state:          %s\n", gw_dcb_state2string(dcb->state));
    if (dcb->session && dcb->session->service)
    {
        dcb_printf(pdcb, "\tService:            %s\n",
                   dcb->session->service->name);
    }
    if (dcb->remote)
    {
        dcb_printf(pdcb, "\tConnected to:               %s\n", dcb->remote);
    }
    if (dcb->user)
    {
        dcb_printf(pdcb, "\tUsername:                   %s\n",
                   dcb->user);
    }
    if (dcb->protoname)
    {
        dcb_printf(pdcb, "\tProtocol:                   %s\n",
                   dcb->protoname);
    }

    if (dcb->session && dcb->session->state != SESSION_STATE_DUMMY)
    {
        dcb_printf(pdcb, "\tOwning Session:     %lu\n", dcb->session->ses_id);
    }

    if (dcb->writeq)
    {
        dcb_printf(pdcb, "\tQueued write data:  %d\n", gwbuf_length(dcb->writeq));
    }
    if (dcb->delayq)
    {
        dcb_printf(pdcb, "\tDelayed write data: %d\n", gwbuf_length(dcb->delayq));
    }
    char *statusname = server_status(dcb->server);
    if (statusname)
    {
        dcb_printf(pdcb, "\tServer status:            %s\n", statusname);
        MXS_FREE(statusname);
    }
    char *rolename = dcb_role_name(dcb);
    if (rolename)
    {
        dcb_printf(pdcb, "\tRole:                     %s\n", rolename);
        MXS_FREE(rolename);
    }
    dcb_printf(pdcb, "\tStatistics:\n");
    dcb_printf(pdcb, "\t\tNo. of Reads:                     %d\n",
               dcb->stats.n_reads);
    dcb_printf(pdcb, "\t\tNo. of Writes:                    %d\n",
               dcb->stats.n_writes);
    dcb_printf(pdcb, "\t\tNo. of Buffered Writes:           %d\n",
               dcb->stats.n_buffered);
    dcb_printf(pdcb, "\t\tNo. of Accepts:                   %d\n",
               dcb->stats.n_accepts);
    dcb_printf(pdcb, "\t\tNo. of High Water Events: %d\n",
               dcb->stats.n_high_water);
    dcb_printf(pdcb, "\t\tNo. of Low Water Events:  %d\n",
               dcb->stats.n_low_water);
    if (DCB_POLL_BUSY(dcb))
    {
        dcb_printf(pdcb, "\t\tPending events in the queue:      %x %s\n",
                   dcb->evq.pending_events, dcb->evq.processing ? "(processing)" : "");
    }
    if (dcb->flags & DCBF_CLONE)
    {
        dcb_printf(pdcb, "\t\tDCB is a clone.\n");
    }

    if (dcb->persistentstart)
    {
        char buff[20];
        struct tm timeinfo;
        localtime_r(&dcb->persistentstart, &timeinfo);
        strftime(buff, sizeof(buff), "%b %d %H:%M:%S", &timeinfo);
        dcb_printf(pdcb, "\t\tAdded to persistent pool:       %s\n", buff);
    }
}

/**
 * Return a string representation of a DCB state.
 *
 * @param state The DCB state
 * @return String representation of the state
 *
 */
const char *
gw_dcb_state2string(dcb_state_t state)
{
    switch (state)
    {
    case DCB_STATE_ALLOC:
        return "DCB Allocated";
    case DCB_STATE_POLLING:
        return "DCB in the polling loop";
    case DCB_STATE_NOPOLLING:
        return "DCB not in polling loop";
    case DCB_STATE_LISTENING:
        return "DCB for listening socket";
    case DCB_STATE_DISCONNECTED:
        return "DCB socket closed";
    case DCB_STATE_ZOMBIE:
        return "DCB Zombie";
    case DCB_STATE_UNDEFINED:
        return "DCB undefined state";
    default:
        return "DCB (unknown - erroneous)";
    }
}

/**
 * A  DCB based wrapper for printf. Allows formatting printing to
 * a descriptor control block.
 *
 * @param dcb   Descriptor to write to
 * @param fmt   A printf format string
 * @param ...   Variable arguments for the print format
 */
void
dcb_printf(DCB *dcb, const char *fmt, ...)
{
    GWBUF   *buf;
    va_list args;

    if ((buf = gwbuf_alloc(10240)) == NULL)
    {
        return;
    }
    va_start(args, fmt);
    vsnprintf((char*)GWBUF_DATA(buf), 10240, fmt, args);
    va_end(args);

    buf->end = (void *)((char *)GWBUF_DATA(buf) + strlen((char*)GWBUF_DATA(buf)));
    dcb->func.write(dcb, buf);
}

/**
 * Print hash table statistics to a DCB
 *
 * @param dcb           The DCB to send the information to
 * @param table         The hash table
 */
void dcb_hashtable_stats(
    DCB     *dcb,
    void    *table)
{
    int total;
    int longest;
    int hashsize;

    total = 0;
    longest = 0;

    hashtable_get_stats(table, &hashsize, &total, &longest);

    dcb_printf(dcb,
               "Hashtable: %p, size %d\n",
               table,
               hashsize);

    dcb_printf(dcb, "\tNo. of entries:      %d\n", total);
    dcb_printf(dcb,
               "\tAverage chain length:        %.1f\n",
               (hashsize == 0 ? (float)hashsize : (float)total / hashsize));
    dcb_printf(dcb, "\tLongest chain length:        %d\n", longest);
}

/**
 * Write data to a DCB socket through an SSL structure. The SSL structure is
 * linked from the DCB. All communication is encrypted and done via the SSL
 * structure. Data is written from the DCB write queue.
 *
 * @param dcb           The DCB having an SSL connection
 * @param writeq        A buffer list containing the data to be written
 * @param stop_writing  Set to true if the caller should stop writing, false otherwise
 * @return              Number of written bytes
 */
static int
gw_write_SSL(DCB *dcb, GWBUF *writeq, bool *stop_writing)
{
    int written;

    written = SSL_write(dcb->ssl, GWBUF_DATA(writeq), GWBUF_LENGTH(writeq));

    *stop_writing = false;
    switch ((SSL_get_error(dcb->ssl, written)))
    {
    case SSL_ERROR_NONE:
        /* Successful write */
        dcb->ssl_write_want_read = false;
        dcb->ssl_write_want_write = false;
        break;

    case SSL_ERROR_ZERO_RETURN:
        /* react to the SSL connection being closed */
        *stop_writing = true;
        poll_fake_hangup_event(dcb);
        break;

    case SSL_ERROR_WANT_READ:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        *stop_writing = true;
        dcb->ssl_write_want_read = true;
        dcb->ssl_write_want_write = false;
        break;

    case SSL_ERROR_WANT_WRITE:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        *stop_writing = true;
        dcb->ssl_write_want_read = false;
        dcb->ssl_write_want_write = true;
        break;

    case SSL_ERROR_SYSCALL:
        *stop_writing = true;
        if (dcb_log_errors_SSL(dcb, __func__, written) < 0)
        {
            poll_fake_hangup_event(dcb);
        }
        break;

    default:
        /* Report error(s) and shutdown the connection */
        *stop_writing = true;
        if (dcb_log_errors_SSL(dcb, __func__, written) < 0)
        {
            poll_fake_hangup_event(dcb);
        }
        break;
    }

    return written > 0 ? written : 0;
}

/**
 * Write data to a DCB. The data is taken from the DCB's write queue.
 *
 * @param dcb           The DCB to write buffer
 * @param writeq        A buffer list containing the data to be written
 * @param stop_writing  Set to true if the caller should stop writing, false otherwise
 * @return              Number of written bytes
 */
static int
gw_write(DCB *dcb, GWBUF *writeq, bool *stop_writing)
{
    int written = 0;
    int fd = dcb->fd;
    size_t nbytes = GWBUF_LENGTH(writeq);
    void *buf = GWBUF_DATA(writeq);
    int saved_errno;

    errno = 0;

    if (fd > 0)
    {
        written = write(fd, buf, nbytes);
    }

    saved_errno = errno;
    errno = 0;

    if (written < 0)
    {
        *stop_writing = true;
#if defined(SS_DEBUG)
        if (saved_errno != EAGAIN &&
            saved_errno != EWOULDBLOCK)
#else
        if (saved_errno != EAGAIN &&
            saved_errno != EWOULDBLOCK &&
            saved_errno != EPIPE)
#endif
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_ERROR("Write to %s %s in state %s failed due errno %d, %s",
                      DCB_STRTYPE(dcb), dcb->remote, STRDCBSTATE(dcb->state),
                      saved_errno, strerror_r(saved_errno, errbuf, sizeof(errbuf)));
            MXS_DEBUG("Write to %s %s in state %s failed due errno %d, %s (at %p, fd %d)",
                      DCB_STRTYPE(dcb), dcb->remote, STRDCBSTATE(dcb->state),
                      saved_errno, strerror_r(saved_errno, errbuf, sizeof(errbuf)),
                      dcb, dcb->fd);
        }
    }
    else
    {
        *stop_writing = false;
    }

    return written > 0 ? written : 0;
}

/**
 * Add a callback
 *
 * Duplicate registrations are not allowed, therefore an error will be
 * returned if the specific function, reason and userdata triple
 * are already registered.
 * An error will also be returned if the is insufficient memeory available to
 * create the registration.
 *
 * @param dcb           The DCB to add the callback to
 * @param reason        The callback reason
 * @param callback      The callback function to call
 * @param userdata      User data to send in the call
 * @return              Non-zero (true) if the callback was added
 */
int
dcb_add_callback(DCB *dcb,
                 DCB_REASON reason,
                 int (*callback)(struct dcb *, DCB_REASON, void *),
                 void *userdata)
{
    DCB_CALLBACK *cb, *ptr, *lastcb = NULL;

    if ((ptr = (DCB_CALLBACK *)MXS_MALLOC(sizeof(DCB_CALLBACK))) == NULL)
    {
        return 0;
    }
    ptr->reason = reason;
    ptr->cb = callback;
    ptr->userdata = userdata;
    ptr->next = NULL;
    cb = dcb->callbacks;

    while (cb)
    {
        if (cb->reason == reason && cb->cb == callback &&
            cb->userdata == userdata)
        {
            /* Callback is a duplicate, abandon it */
            MXS_FREE(ptr);
            return 0;
        }
        lastcb = cb;
        cb = cb->next;
    }
    if (NULL == lastcb)
    {
        dcb->callbacks = ptr;
    }
    else
    {
        lastcb->next = ptr;
    }

    return 1;
}

/**
 * Remove a callback from the callback list for the DCB
 *
 * Searches down the linked list to find the callback with a matching reason, function
 * and userdata.
 *
 * @param dcb           The DCB to add the callback to
 * @param reason        The callback reason
 * @param callback      The callback function to call
 * @param userdata      User data to send in the call
 * @return              Non-zero (true) if the callback was removed
 */
int
dcb_remove_callback(DCB *dcb,
                    DCB_REASON reason,
                    int (*callback)(struct dcb *, DCB_REASON, void *),
                    void *userdata)
{
    DCB_CALLBACK *cb, *pcb = NULL;
    int          rval = 0;
    cb = dcb->callbacks;

    if (cb == NULL)
    {
        rval = 0;
    }
    else
    {
        while (cb)
        {
            if (cb->reason == reason &&
                cb->cb == callback &&
                cb->userdata == userdata)
            {
                if (pcb != NULL)
                {
                    pcb->next = cb->next;
                }
                else
                {
                    dcb->callbacks = cb->next;
                }

                MXS_FREE(cb);
                rval = 1;
                break;
            }
            pcb = cb;
            cb = cb->next;
        }
    }

    return rval;
}

/**
 * Call the set of callbacks registered for a particular reason.
 *
 * @param dcb           The DCB to call the callbacks regarding
 * @param reason        The reason that has triggered the call
 */
static void
dcb_call_callback(DCB *dcb, DCB_REASON reason)
{
    DCB_CALLBACK *cb, *nextcb;
    cb = dcb->callbacks;

    while (cb)
    {
        if (cb->reason == reason)
        {
            nextcb = cb->next;

            MXS_DEBUG("%lu [dcb_call_callback] %s",
                      pthread_self(),
                      STRDCBREASON(reason));

            cb->cb(dcb, reason, cb->userdata);
            cb = nextcb;
        }
        else
        {
            cb = cb->next;
        }
    }
}

/**
 * Check the passed DCB to ensure it is in the list of all DCBS
 *
 * @param       dcb     The DCB to check
 * @return      1 if the DCB is in the list, otherwise 0
 */
int
dcb_isvalid(DCB *dcb)
{
    return dcb && !dcb->dcb_is_zombie;
}

/**
 * Call all the callbacks on all DCB's that match the server and the reason given
 *
 * @param reason        The DCB_REASON that triggers the callback
 */
void
dcb_hangup_foreach(struct server* server)
{
    int nthr = config_threadcount();


    for (int i = 0; i < nthr; i++)
    {
        spinlock_acquire(&all_dcbs_lock[i]);

        for (DCB *dcb = all_dcbs[i]; dcb; dcb = dcb->thread.next)
        {
            if (dcb->state == DCB_STATE_POLLING && dcb->server &&
                dcb->server == server)
            {
                poll_fake_hangup_event(dcb);
            }
        }

        spinlock_release(&all_dcbs_lock[i]);
    }
}

/**
 * Null protocol write routine used for cloned dcb's. It merely consumes
 * buffers written on the cloned DCB and sets the DCB_REPLIED flag.
 *
 * @param dcb           The descriptor control block
 * @param buf           The buffer being written
 * @return      Always returns a good write operation result
 */
static int
dcb_null_write(DCB *dcb, GWBUF *buf)
{
    while (buf)
    {
        buf = gwbuf_consume(buf, GWBUF_LENGTH(buf));
    }

    dcb->flags |= DCBF_REPLIED;

    return 1;
}

/**
 * Null protocol auth operation for use by cloned DCB's.
 *
 * @param dcb           The DCB being closed.
 * @param server        The server to auth against
 * @param session       The user session
 * @param buf           The buffer with the new auth request
 */
static int
dcb_null_auth(DCB *dcb, SERVER *server, MXS_SESSION *session, GWBUF *buf)
{
    return 0;
}

/**
 * Check persistent pool for expiry or excess size and count
 *
 * @param dcb           The DCB being closed.
 * @param id            Thread ID
 * @param cleanall      Boolean, if true the whole pool is cleared for the
 *                      server related to the given DCB
 * @return              A count of the DCBs remaining in the pool
 */
int
dcb_persistent_clean_count(DCB *dcb, int id, bool cleanall)
{
    int count = 0;
    if (dcb && dcb->server)
    {
        SERVER *server = dcb->server;
        DCB *previousdcb = NULL;
        DCB *persistentdcb, *nextdcb;
        DCB *disposals = NULL;

        CHK_SERVER(server);
        persistentdcb = server->persistent[id];
        while (persistentdcb)
        {
            CHK_DCB(persistentdcb);
            nextdcb = persistentdcb->nextpersistent;
            if (cleanall
                || persistentdcb-> dcb_errhandle_called
                || count >= server->persistpoolmax
                || persistentdcb->server == NULL
                || !(persistentdcb->server->status & SERVER_RUNNING)
                || (time(NULL) - persistentdcb->persistentstart) > server->persistmaxtime)
            {
                /* Remove from persistent pool */
                if (previousdcb)
                {
                    previousdcb->nextpersistent = nextdcb;
                }
                else
                {
                    server->persistent[id] = nextdcb;
                }
                /* Add removed DCBs to disposal list for processing outside spinlock */
                persistentdcb->nextpersistent = disposals;
                disposals = persistentdcb;
                atomic_add(&server->stats.n_persistent, -1);
            }
            else
            {
                count++;
                previousdcb = persistentdcb;
            }
            persistentdcb = nextdcb;
        }
        server->persistmax = MXS_MAX(server->persistmax, count);

        /** Call possible callback for this DCB in case of close */
        while (disposals)
        {
            nextdcb = disposals->nextpersistent;
            disposals->persistentstart = -1;
            if (DCB_STATE_POLLING == disposals->state)
            {
                dcb_stop_polling_and_shutdown(disposals);
            }
            dcb_close(disposals);
            disposals = nextdcb;
        }
    }
    return count;
}

struct dcb_usage_count
{
    int count;
    DCB_USAGE type;
};

bool count_by_usage_cb(DCB *dcb, void *data)
{
    struct dcb_usage_count *d = (struct dcb_usage_count*)data;

    switch (d->type)
    {
    case DCB_USAGE_CLIENT:
        if (DCB_ROLE_CLIENT_HANDLER == dcb->dcb_role)
        {
            d->count++;
        }
        break;
    case DCB_USAGE_LISTENER:
        if (dcb->state == DCB_STATE_LISTENING)
        {
            d->count++;
        }
        break;
    case DCB_USAGE_BACKEND:
        if (dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER)
        {
            d->count++;
        }
        break;
    case DCB_USAGE_INTERNAL:
        if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER ||
            dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER)
        {
            d->count++;
        }
        break;
    case DCB_USAGE_ZOMBIE:
        if (DCB_ISZOMBIE(dcb))
        {
            d->count++;
        }
        break;
    case DCB_USAGE_ALL:
        d->count++;
        break;
    }

    return true;
}

/**
 * Return DCB counts optionally filtered by usage
 *
 * @param       usage   The usage of the DCB
 * @return      A count of DCBs in the desired state
 */
int
dcb_count_by_usage(DCB_USAGE usage)
{
    struct dcb_usage_count val = {.count = 0, .type = usage};

    dcb_foreach(count_by_usage_cb, &val);

    return val.count;
}

/**
 * Create the SSL structure for this DCB.
 * This function creates the SSL structure for the given SSL context.
 * This context should be the context of the service.
 * @param       dcb
 * @return      -1 on error, 0 otherwise.
 */
static int
dcb_create_SSL(DCB* dcb, SSL_LISTENER *ssl)
{
    if ((dcb->ssl = SSL_new(ssl->ctx)) == NULL)
    {
        MXS_ERROR("Failed to initialize SSL for connection.");
        return -1;
    }

    if (SSL_set_fd(dcb->ssl, dcb->fd) == 0)
    {
        MXS_ERROR("Failed to set file descriptor for SSL connection.");
        return -1;
    }

    return 0;
}

/**
 * Accept a SSL connection and do the SSL authentication handshake.
 * This function accepts a client connection to a DCB. It assumes that the SSL
 * structure has the underlying method of communication set and this method is ready
 * for usage. It then proceeds with the SSL handshake and stops only if an error
 * occurs or the client has not yet written enough data to complete the handshake.
 * @param dcb DCB which should accept the SSL connection
 * @return 1 if the handshake was successfully completed, 0 if the handshake is
 * still ongoing and another call to dcb_SSL_accept should be made or -1 if an
 * error occurred during the handshake and the connection should be terminated.
 */
int dcb_accept_SSL(DCB* dcb)
{
    if ((NULL == dcb->listener || NULL == dcb->listener->ssl) ||
        (NULL == dcb->ssl && dcb_create_SSL(dcb, dcb->listener->ssl) != 0))
    {
        return -1;
    }

    ss_debug(char *remote = dcb->remote ? dcb->remote : "");
    ss_debug(char *user = dcb->user ? dcb->user : "");

    int ssl_rval = SSL_accept(dcb->ssl);

    switch (SSL_get_error(dcb->ssl, ssl_rval))
    {
    case SSL_ERROR_NONE:
        MXS_DEBUG("SSL_accept done for %s@%s", user, remote);
        dcb->ssl_state = SSL_ESTABLISHED;
        dcb->ssl_read_want_write = false;
        return 1;

    case SSL_ERROR_WANT_READ:
        MXS_DEBUG("SSL_accept ongoing want read for %s@%s", user, remote);
        return 0;

    case SSL_ERROR_WANT_WRITE:
        MXS_DEBUG("SSL_accept ongoing want write for %s@%s", user, remote);
        dcb->ssl_read_want_write = true;
        return 0;

    case SSL_ERROR_ZERO_RETURN:
        MXS_DEBUG("SSL error, shut down cleanly during SSL accept %s@%s", user, remote);
        dcb_log_errors_SSL(dcb, __func__, 0);
        poll_fake_hangup_event(dcb);
        return 0;

    case SSL_ERROR_SYSCALL:
        MXS_DEBUG("SSL connection SSL_ERROR_SYSCALL error during accept %s@%s", user, remote);
        if (dcb_log_errors_SSL(dcb, __func__, ssl_rval) < 0)
        {
            dcb->ssl_state = SSL_HANDSHAKE_FAILED;
            poll_fake_hangup_event(dcb);
            return -1;
        }
        else
        {
            return 0;
        }

    default:
        MXS_DEBUG("SSL connection shut down with error during SSL accept %s@%s", user, remote);
        if (dcb_log_errors_SSL(dcb, __func__, ssl_rval) < 0)
        {
            dcb->ssl_state = SSL_HANDSHAKE_FAILED;
            poll_fake_hangup_event(dcb);
            return -1;
        }
        else
        {
            return 0;
        }
    }
}

/**
 * Initiate an SSL client connection to a server
 *
 * This functions starts an SSL client connection to a server which is expecting
 * an SSL handshake. The DCB should already have a TCP connection to the server and
 * this connection should be in a state that expects an SSL handshake.
 * THIS CODE IS UNUSED AND UNTESTED as at 4 Jan 2016
 * @param dcb DCB to connect
 * @return 1 on success, -1 on error and 0 if the SSL handshake is still ongoing
 */
int dcb_connect_SSL(DCB* dcb)
{
    int ssl_rval;
    int return_code;

    if ((NULL == dcb->server || NULL == dcb->server->server_ssl) ||
        (NULL == dcb->ssl && dcb_create_SSL(dcb, dcb->server->server_ssl) != 0))
    {
        ss_dassert((NULL != dcb->server) && (NULL != dcb->server->server_ssl));
        return -1;
    }
    dcb->ssl_state = SSL_HANDSHAKE_REQUIRED;
    ssl_rval = SSL_connect(dcb->ssl);
    switch (SSL_get_error(dcb->ssl, ssl_rval))
    {
    case SSL_ERROR_NONE:
        MXS_DEBUG("SSL_connect done for %s", dcb->remote);
        dcb->ssl_state = SSL_ESTABLISHED;
        dcb->ssl_read_want_write = false;
        return_code = 1;
        break;

    case SSL_ERROR_WANT_READ:
        MXS_DEBUG("SSL_connect ongoing want read for %s", dcb->remote);
        return_code = 0;
        break;

    case SSL_ERROR_WANT_WRITE:
        MXS_DEBUG("SSL_connect ongoing want write for %s", dcb->remote);
        dcb->ssl_read_want_write = true;
        return_code = 0;
        break;

    case SSL_ERROR_ZERO_RETURN:
        MXS_DEBUG("SSL error, shut down cleanly during SSL connect %s", dcb->remote);
        if (dcb_log_errors_SSL(dcb, __func__, 0) < 0)
        {
            poll_fake_hangup_event(dcb);
        }
        return_code = 0;
        break;

    case SSL_ERROR_SYSCALL:
        MXS_DEBUG("SSL connection shut down with SSL_ERROR_SYSCALL during SSL connect %s", dcb->remote);
        if (dcb_log_errors_SSL(dcb, __func__, ssl_rval) < 0)
        {
            dcb->ssl_state = SSL_HANDSHAKE_FAILED;
            poll_fake_hangup_event(dcb);
            return_code = -1;
        }
        else
        {
            return_code = 0;
        }
        break;

    default:
        MXS_DEBUG("SSL connection shut down with error during SSL connect %s", dcb->remote);
        if (dcb_log_errors_SSL(dcb, __func__, ssl_rval) < 0)
        {
            dcb->ssl_state = SSL_HANDSHAKE_FAILED;
            poll_fake_hangup_event(dcb);
            return -1;
        }
        else
        {
            return 0;
        }
        break;
    }
    return return_code;
}

/**
 * @brief Accept a new client connection, given a listener, return new DCB
 *
 * Calls dcb_accept_one_connection to do the basic work of obtaining a new
 * connection from a listener.  If that succeeds, some settings are fixed and
 * a client DCB is created to handle the new connection. Further DCB details
 * are set before returning the new DCB to the caller, or returning NULL if
 * no new connection could be achieved.
 *
 * @param dcb Listener DCB that has detected new connection request
 * @return DCB - The new client DCB for the new connection, or NULL if failed
 */
DCB *
dcb_accept(DCB *listener)
{
    DCB *client_dcb = NULL;
    MXS_PROTOCOL *protocol_funcs = &listener->func;
    int c_sock;
    int sendbuf;
    struct sockaddr_storage client_conn;
    socklen_t optlen = sizeof(sendbuf);
    char errbuf[MXS_STRERROR_BUFLEN];

    if ((c_sock = dcb_accept_one_connection(listener, (struct sockaddr *)&client_conn)) >= 0)
    {
        listener->stats.n_accepts++;
        MXS_DEBUG("%lu [gw_MySQLAccept] Accepted fd %d.",
                  pthread_self(),
                  c_sock);
        /* set nonblocking  */
        sendbuf = MXS_CLIENT_SO_SNDBUF;

        if (setsockopt(c_sock, SOL_SOCKET, SO_SNDBUF, &sendbuf, optlen) != 0)
        {
            MXS_ERROR("Failed to set socket options. Error %d: %s",
                      errno, strerror_r(errno, errbuf, sizeof(errbuf)));
        }

        sendbuf = MXS_CLIENT_SO_RCVBUF;

        if (setsockopt(c_sock, SOL_SOCKET, SO_RCVBUF, &sendbuf, optlen) != 0)
        {
            MXS_ERROR("Failed to set socket options. Error %d: %s",
                      errno, strerror_r(errno, errbuf, sizeof(errbuf)));
        }
        setnonblocking(c_sock);

        client_dcb = dcb_alloc(DCB_ROLE_CLIENT_HANDLER, listener->listener);

        if (client_dcb == NULL)
        {
            MXS_ERROR("Failed to create DCB object for client connection.");
            close(c_sock);
        }
        else
        {
            const char *authenticator_name = "NullAuthDeny";
            MXS_AUTHENTICATOR *authfuncs;

            client_dcb->service = listener->session->service;
            client_dcb->session = session_set_dummy(client_dcb);
            client_dcb->fd = c_sock;

            // get client address
            if (client_conn.ss_family == AF_UNIX)
            {
                // client address
                client_dcb->remote = MXS_STRDUP_A("localhost");
            }
            else
            {
                /* client IP in raw data*/
                memcpy(&client_dcb->ip, &client_conn, sizeof(client_conn));
                /* client IP in string representation */
                client_dcb->remote = (char *)MXS_CALLOC(INET6_ADDRSTRLEN + 1, sizeof(char));

                if (client_dcb->remote)
                {
                    void *ptr;
                    if (client_dcb->ip.ss_family == AF_INET)
                    {
                        ptr = &((struct sockaddr_in*)&client_dcb->ip)->sin_addr;
                    }
                    else
                    {
                        ptr = &((struct sockaddr_in6*)&client_dcb->ip)->sin6_addr;
                    }

                    inet_ntop(client_dcb->ip.ss_family, ptr,
                              client_dcb->remote, INET6_ADDRSTRLEN);
                }
            }
            memcpy(&client_dcb->func, protocol_funcs, sizeof(MXS_PROTOCOL));
            if (listener->listener->authenticator)
            {
                authenticator_name = listener->listener->authenticator;
            }
            else if (client_dcb->func.auth_default != NULL)
            {
                authenticator_name = client_dcb->func.auth_default();
            }
            if ((authfuncs = (MXS_AUTHENTICATOR *)load_module(authenticator_name,
                                                            MODULE_AUTHENTICATOR)) == NULL)
            {
                if ((authfuncs = (MXS_AUTHENTICATOR *)load_module("NullAuthDeny",
                                                                MODULE_AUTHENTICATOR)) == NULL)
                {
                    MXS_ERROR("Failed to load authenticator module for %s, free dcb %p\n",
                              authenticator_name,
                              client_dcb);
                    dcb_close(client_dcb);
                    return NULL;
                }
            }
            memcpy(&(client_dcb->authfunc), authfuncs, sizeof(MXS_AUTHENTICATOR));

            /** Allocate DCB specific authentication data */
            if (client_dcb->authfunc.create &&
                (client_dcb->authenticator_data = client_dcb->authfunc.create(
                                                      client_dcb->listener->auth_instance)) == NULL)
            {
                MXS_ERROR("Failed to create authenticator for client DCB.");
                dcb_close(client_dcb);
                return NULL;
            }

            if (client_dcb->service->max_connections &&
                client_dcb->service->client_count >= client_dcb->service->max_connections)
            {
                if (!mxs_enqueue(client_dcb->service->queued_connections, client_dcb))
                {
                    if (client_dcb->func.connlimit)
                    {
                        client_dcb->func.connlimit(client_dcb, client_dcb->service->max_connections);
                    }
                    dcb_close(client_dcb);
                }
                client_dcb = NULL;
            }
        }
    }
    return client_dcb;
}

/**
 * @brief Accept a new client connection, given listener, return file descriptor
 *
 * Up to 10 retries will be attempted in case of non-permanent errors.  Calls
 * the accept function and analyses the return, logging any errors and making
 * an appropriate return.
 *
 * @param dcb Listener DCB that has detected new connection request
 * @return -1 for failure, or a file descriptor for the new connection
 */
static int
dcb_accept_one_connection(DCB *listener, struct sockaddr *client_conn)
{
    int c_sock;

    /* Try up to 10 times to get a file descriptor by use of accept */
    for (int i = 0; i < 10; i++)
    {
        socklen_t client_len = sizeof(struct sockaddr_storage);
        int eno = 0;

        /* new connection from client */
        c_sock = accept(listener->fd,
                        client_conn,
                        &client_len);
        eno = errno;
        errno = 0;

        if (c_sock == -1)
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            /* Did not get a file descriptor */
            if (eno == EAGAIN || eno == EWOULDBLOCK)
            {
                /**
                 * We have processed all incoming connections, break out
                 * of loop for return of -1.
                 */
                break;
            }
            else if (eno == ENFILE || eno == EMFILE)
            {
                struct timespec ts1;
                long long nanosecs;

                /**
                 * Exceeded system's (ENFILE) or processes
                 * (EMFILE) max. number of files limit.
                 */
                MXS_DEBUG("%lu [dcb_accept_one_connection] Error %d, %s. ",
                          pthread_self(),
                          eno,
                          strerror_r(eno, errbuf, sizeof(errbuf)));

                /* Log an error the first time this happens */
                if (i == 0)
                {
                    MXS_ERROR("Error %d, %s. Failed to accept new client connection.",
                              eno,
                              strerror_r(eno, errbuf, sizeof(errbuf)));
                }
                nanosecs = (long long)1000000 * 100 * i * i;
                ts1.tv_sec = nanosecs / 1000000000;
                ts1.tv_nsec = nanosecs % 1000000000;
                nanosleep(&ts1, NULL);

                /* Remain in loop for up to the loop limit, retries. */
            }
            else
            {
                /**
                 * Other error, log it then break out of loop for return of -1.
                 */
                MXS_ERROR("Failed to accept new client connection due to %d, %s.",
                          eno,
                          strerror_r(eno, errbuf, sizeof(errbuf)));
                break;
            }
        }
        else
        {
            break;
        }
    }
    return c_sock;
}

/**
 * @brief Create a listener, add new information to the given DCB
 *
 * First creates and opens a socket, either TCP or Unix according to the
 * configuration data provided.  Then try to listen on the socket and
 * record the socket in the given DCB.  Add the given DCB into the poll
 * list.  The protocol name does not affect the logic, but is used in
 * log messages.
 *
 * @param listener Listener DCB that is being created
 * @param config Configuration for port to listen on
 * @param protocol_name Name of protocol that is listening
 * @return 0 if new listener created successfully, otherwise -1
 */
int dcb_listen(DCB *listener, const char *config, const char *protocol_name)
{
    char host[strlen(config) + 1];
    strcpy(host, config);
    char *port_str = strrchr(host, '|');
    uint16_t port = 0;

    if (port_str)
    {
        *port_str++ = 0;
        port = atoi(port_str);
    }

    int listener_socket = -1;

    if (strchr(host, '/'))
    {
        listener_socket = dcb_listen_create_socket_unix(host);
    }
    else if (port > 0)
    {
        listener_socket = dcb_listen_create_socket_inet(host, port);

        if (listener_socket == -1 && strcmp(host, "::") == 0)
        {
            /** Attempt to bind to the IPv4 if the default IPv6 one is used */
            MXS_WARNING("Failed to bind on default IPv6 host '::', attempting "
                        "to bind on IPv4 version '0.0.0.0'");
            strcpy(host, "0.0.0.0");
            listener_socket = dcb_listen_create_socket_inet(host, port);
        }
    }
    else
    {
        // We don't have a socket path or a network port
        ss_dassert(false);
    }

    if (listener_socket < 0)
    {
        ss_dassert(listener_socket == -1);
        return -1;
    }

    /**
     * The use of INT_MAX for backlog length in listen() allows the end-user to
     * control the backlog length with the net.ipv4.tcp_max_syn_backlog kernel
     * option since the parameter is silently truncated to the configured value.
     *
     * @see man 2 listen
     */
    if (listen(listener_socket, INT_MAX) != 0)
    {
        MXS_ERROR("Failed to start listening on '[%s]:%u' with protocol '%s': %d, %s",
                  host, port, protocol_name, errno, mxs_strerror(errno));
        close(listener_socket);
        return -1;
    }

    MXS_NOTICE("Listening for connections at [%s]:%u with protocol %s", host, port, protocol_name);

    // assign listener_socket to dcb
    listener->fd = listener_socket;

    // add listening socket to poll structure
    if (poll_add_dcb(listener) != 0)
    {
        MXS_ERROR("MaxScale encountered system limit while "
                  "attempting to register on an epoll instance.");
        return -1;
    }
    return 0;
}

/**
 * @brief Create a network listener socket
 *
 * @param host The network address to listen on
 * @param port The port to listen on
 * @return     The opened socket or -1 on error
 */
static int dcb_listen_create_socket_inet(const char *host, uint16_t port)
{
    struct sockaddr_storage server_address = {};
    int listener_socket = open_network_socket(MXS_SOCKET_LISTENER, &server_address, host, port);

    if (listener_socket != -1)
    {
        if (bind(listener_socket, (struct sockaddr*)&server_address, sizeof(server_address)) < 0)
        {
            MXS_ERROR("Failed to bind on '%s:%u': %d, %s",
                      host, port, errno, mxs_strerror(errno));
            close(listener_socket);
            listener_socket = -1;
        }
    }

    return listener_socket;
}

/**
 * @brief Create a Unix domain socket
 *
 * @param path The socket path
 * @return     The opened socket or -1 on error
 */
static int dcb_listen_create_socket_unix(const char *path)
{
    int listener_socket;
    struct sockaddr_un local_addr;
    int one = 1;

    if (strlen(path) > sizeof(local_addr.sun_path) - 1)
    {
        MXS_ERROR("The path %s specified for the UNIX domain socket is too long. "
                  "The maximum length is %lu.", path, sizeof(local_addr.sun_path) - 1);
        return -1;
    }

    // UNIX socket create
    if ((listener_socket = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
    {
        MXS_ERROR("Can't create UNIX socket: %d, %s", errno, mxs_strerror(errno));
        return -1;
    }

    // socket options
    if (dcb_set_socket_option(listener_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)) != 0)
    {
        return -1;
    }

    // set NONBLOCKING mode
    if (setnonblocking(listener_socket) != 0)
    {
        MXS_ERROR("Failed to set socket to non-blocking mode.");
        close(listener_socket);
        return -1;
    }

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sun_family = AF_UNIX;
    strcpy(local_addr.sun_path, path);

    if ((-1 == unlink(path)) && (errno != ENOENT))
    {
        MXS_ERROR("Failed to unlink Unix Socket %s: %d %s",
                  path, errno, mxs_strerror(errno));
    }

    /* Bind the socket to the Unix domain socket */
    if (bind(listener_socket, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
    {
        MXS_ERROR("Failed to bind to UNIX Domain socket '%s': %d, %s",
                  path, errno, mxs_strerror(errno));
        close(listener_socket);
        return -1;
    }

    /* set permission for all users */
    if (chmod(path, 0777) < 0)
    {
        MXS_ERROR("Failed to change permissions on UNIX Domain socket '%s': %d, %s",
                  path, errno, mxs_strerror(errno));
    }

    return listener_socket;
}

/**
 * @brief Set socket options, log an error if fails
 *
 * Simply calls the setsockopt function with the same parameters, but also
 * checks for success and logs an error if necessary.
 *
 * @param sockfd  Socket file descriptor
 * @param level   Will always be SOL_SOCKET for socket level operations
 * @param optname Option name
 * @param optval  Option value
 * @param optlen  Length of option value
 * @return 0 if successful, otherwise -1
 */
static int
dcb_set_socket_option(int sockfd, int level, int optname, void *optval, socklen_t optlen)
{
    if (setsockopt(sockfd, level, optname, optval, optlen) != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        MXS_ERROR("Failed to set socket options. Error %d: %s",
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        return -1;
    }
    return 0;
}

/**
 * Convert a DCB role to a string, the returned
 * string has been malloc'd and must be free'd by the caller
 *
 * @param DCB    The DCB to return the role of
 * @return       A string representation of the DCB role
 */
char *
dcb_role_name(DCB *dcb)
{
    char *name = (char *)MXS_MALLOC(64);

    if (name)
    {
        name[0] = 0;
        if (DCB_ROLE_SERVICE_LISTENER == dcb->dcb_role)
        {
            strcat(name, "Service Listener");
        }
        else if (DCB_ROLE_CLIENT_HANDLER == dcb->dcb_role)
        {
            strcat(name, "Client Request Handler");
        }
        else if (DCB_ROLE_BACKEND_HANDLER == dcb->dcb_role)
        {
            strcat(name, "Backend Request Handler");
        }
        else if (DCB_ROLE_INTERNAL == dcb->dcb_role)
        {
            strcat(name, "Internal");
        }
        else
        {
            strcat(name, "Unknown");
        }
    }
    return name;
}

/**
 * @brief Append a buffer the DCB's readqueue
 *
 * Usually data is stored into the DCB's readqueue when not enough data is
 * available and the processing needs to be deferred until more data is available.
 *
 * @param dcb DCB where the buffer is stored
 * @param buffer Buffer to store
 */
void dcb_append_readqueue(DCB *dcb, GWBUF *buffer)
{
    dcb->dcb_readqueue = gwbuf_append(dcb->dcb_readqueue, buffer);
}

void dcb_add_to_list(DCB *dcb)
{
    if (dcb->dcb_role != DCB_ROLE_SERVICE_LISTENER ||
        (dcb->thread.next == NULL && dcb->thread.tail == NULL))
    {
        /**
         * This is a DCB which is either not a listener or it is a listener which
         * is not in the list. Stopped listeners are not removed from the list
         * as that part is done in the final zombie processing.
         */

        spinlock_acquire(&all_dcbs_lock[dcb->thread.id]);

        if (all_dcbs[dcb->thread.id] == NULL)
        {
            all_dcbs[dcb->thread.id] = dcb;
            all_dcbs[dcb->thread.id]->thread.tail = dcb;
        }
        else
        {
            all_dcbs[dcb->thread.id]->thread.tail->thread.next = dcb;
            all_dcbs[dcb->thread.id]->thread.tail = dcb;
        }

        spinlock_release(&all_dcbs_lock[dcb->thread.id]);
    }
}

/**
 * Remove a DCB from the owner's list
 *
 * @param dcb DCB to remove
 */
static void dcb_remove_from_list(DCB *dcb)
{
    spinlock_acquire(&all_dcbs_lock[dcb->thread.id]);

    if (dcb == all_dcbs[dcb->thread.id])
    {
        DCB *tail = all_dcbs[dcb->thread.id]->thread.tail;
        all_dcbs[dcb->thread.id] = all_dcbs[dcb->thread.id]->thread.next;

        if (all_dcbs[dcb->thread.id])
        {
            all_dcbs[dcb->thread.id]->thread.tail = tail;
        }
    }
    else
    {
        DCB *current = all_dcbs[dcb->thread.id]->thread.next;
        DCB *prev = all_dcbs[dcb->thread.id];

        while (current)
        {
            if (current == dcb)
            {
                if (current == all_dcbs[dcb->thread.id]->thread.tail)
                {
                    all_dcbs[dcb->thread.id]->thread.tail = prev;
                }
                prev->thread.next = current->thread.next;
                break;
            }
            prev = current;
            current = current->thread.next;
        }
    }

    /** Reset the next and tail pointers so that if this DCB is added to the list
     * again, it will be in a clean state. */
    dcb->thread.next = NULL;
    dcb->thread.tail = NULL;

    spinlock_release(&all_dcbs_lock[dcb->thread.id]);
}

/**
 * Enable the timing out of idle connections.
 */
void dcb_enable_session_timeouts()
{
    check_timeouts = true;
}

/**
 * Close sessions that have been idle for too long.
 *
 * If the time since a session last sent data is greater than the set value in the
 * service, it is disconnected. The connection timeout is disabled by default.
 */
void dcb_process_idle_sessions(int thr)
{
    if (check_timeouts && hkheartbeat >= next_timeout_check)
    {
        /** Because the resolution of the timeout is one second, we only need to
         * check for it once per second. One heartbeat is 100 milliseconds. */
        next_timeout_check = hkheartbeat + 10;

        for (DCB *dcb = all_dcbs[thr]; dcb; dcb = dcb->thread.next)
        {
            if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER)
            {
                ss_dassert(dcb->listener);
                SERVICE *service = dcb->listener->service;

                if (service->conn_idle_timeout && dcb->state == DCB_STATE_POLLING)
                {
                    int64_t idle = hkheartbeat - dcb->last_read;
                    int64_t timeout = service->conn_idle_timeout * 10;

                    if (idle > timeout)
                    {
                        MXS_WARNING("Timing out '%s'@%s, idle for %.1f seconds",
                                    dcb->user ? dcb->user : "<unknown>",
                                    dcb->remote ? dcb->remote : "<unknown>",
                                    (float)idle / 10.f);
                        poll_fake_hangup_event(dcb);
                    }
                }
            }
        }
    }
}

bool dcb_foreach(bool(*func)(DCB *, void *), void *data)
{

    int nthr = config_threadcount();
    bool more = true;

    for (int i = 0; i < nthr && more; i++)
    {
        spinlock_acquire(&all_dcbs_lock[i]);

        for (DCB *dcb = all_dcbs[i]; dcb && more; dcb = dcb->thread.next)
        {
            if (!func(dcb, data))
            {
                more = false;
            }
        }

        spinlock_release(&all_dcbs_lock[i]);
    }

    return more;
}

int dcb_get_port(const DCB *dcb)
{
    int rval = -1;

    if (dcb->ip.ss_family == AF_INET)
    {
        struct sockaddr_in* ip = (struct sockaddr_in*)&dcb->ip;
        rval = ntohs(ip->sin_port);
    }
    else if (dcb->ip.ss_family == AF_INET6)
    {
        struct sockaddr_in6* ip = (struct sockaddr_in6*)&dcb->ip;
        rval = ntohs(ip->sin6_port);
    }
    else
    {
        ss_dassert(dcb->ip.ss_family == AF_UNIX);
    }

    return rval;
}
