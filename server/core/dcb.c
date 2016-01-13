/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
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
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <dcb.h>
#include <spinlock.h>
#include <server.h>
#include <session.h>
#include <service.h>
#include <modules.h>
#include <router.h>
#include <errno.h>
#include <gw.h>
#include <poll.h>
#include <atomic.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <hashtable.h>
#include <hk_heartbeat.h>

#define SSL_ERRBUF_LEN 140

static  DCB             *allDCBs = NULL;        /* Diagnostics need a list of DCBs */
static  int             nDCBs = 0;
static  int             maxDCBs = 0;
static  DCB             *zombies = NULL;
static  int             nzombies = 0;
static  int             maxzombies = 0;
static  SPINLOCK        dcbspin = SPINLOCK_INIT;
static  SPINLOCK        zombiespin = SPINLOCK_INIT;

static void dcb_final_free(DCB *dcb);
static void dcb_call_callback(DCB *dcb, DCB_REASON reason);
static DCB * dcb_get_next (DCB *dcb);
static int  dcb_null_write(DCB *dcb, GWBUF *buf);
static int  dcb_null_close(DCB *dcb);
static int  dcb_null_auth(DCB *dcb, SERVER *server, SESSION *session, GWBUF *buf);
static inline int  dcb_isvalid_nolock(DCB *dcb);
static inline DCB * dcb_find_in_list(DCB *dcb);
static inline void dcb_process_victim_queue(DCB *listofdcb);
static void dcb_stop_polling_and_shutdown (DCB *dcb);
static bool dcb_maybe_add_persistent(DCB *);
static inline bool dcb_write_parameter_check(DCB *dcb, GWBUF *queue);
static int dcb_bytes_readable(DCB *dcb);
static int dcb_read_no_bytes_available(DCB *dcb, int nreadtotal);
static GWBUF *dcb_basic_read(DCB *dcb, int bytesavailable, int maxbytes, int nreadtotal, int *nsingleread);
static GWBUF *dcb_basic_read_SSL(DCB *dcb, int *nsingleread);
#if defined(FAKE_CODE)
static inline void dcb_write_fake_code(DCB *dcb);
#endif
static void dcb_log_write_failure(DCB *dcb, GWBUF *queue, int eno);
static inline void dcb_write_tidy_up(DCB *dcb, bool below_water);
static void dcb_log_ssl_read_error(DCB *dcb, int ssl_errno, int rc);
static int gw_write(DCB *dcb, bool *stop_writing);
static int gw_write_SSL(DCB *dcb, bool *stop_writing);
static void dcb_log_errors_SSL (DCB *dcb, const char *called_by, int ret);

size_t dcb_get_session_id(
    DCB *dcb)
{
    return (dcb && dcb->session) ? dcb->session->ses_id : 0;
}

/**
 * Read log info from session through DCB and store values to memory locations
 * passed as parameters.
 *
 * @param dcb                     DCB
 * @param sesid                   location where session id is to be copied
 * @param enabled_log_prioritiess bit field indicating which log types are enabled for the
 * session
 *
 *@return true if call arguments included memory addresses, false if any of the
 *        parameters was NULL.
 */
bool dcb_get_ses_log_info(
    DCB     *dcb,
    size_t  *sesid,
    int     *enabled_log_priorities)
{
    if (sesid && enabled_log_priorities && dcb && dcb->session)
    {
        *sesid = dcb->session->ses_id;
        *enabled_log_priorities = dcb->session->enabled_log_priorities;
        return true;
    }
    return false;
}

/**
 * Return the pointer to the list of zombie DCB's
 *
 * @return Zombies DCB list
 */
DCB *
dcb_get_zombies(void)
{
    return zombies;
}

/**
 * Allocate a new DCB.
 *
 * This routine performs the generic initialisation on the DCB before returning
 * the newly allocated DCB.
 *
 * @param dcb_role_t    The role for the new DCB
 * @return A newly allocated DCB or NULL if non could be allocated.
 */
DCB *
dcb_alloc(dcb_role_t role)
{
    DCB *newdcb;

    if ((newdcb = calloc(1, sizeof(DCB))) == NULL)
    {
        return NULL;
    }
    newdcb->dcb_chk_top = CHK_NUM_DCB;
    newdcb->dcb_chk_tail = CHK_NUM_DCB;

    newdcb->dcb_errhandle_called = false;
    newdcb->dcb_role = role;
    spinlock_init(&newdcb->dcb_initlock);
    spinlock_init(&newdcb->writeqlock);
    spinlock_init(&newdcb->delayqlock);
    spinlock_init(&newdcb->authlock);
    spinlock_init(&newdcb->cb_lock);
    spinlock_init(&newdcb->pollinlock);
    spinlock_init(&newdcb->polloutlock);
    newdcb->pollinbusy = 0;
    newdcb->readcheck = 0;
    newdcb->polloutbusy = 0;
    newdcb->writecheck = 0;
    newdcb->fd = DCBFD_CLOSED;

    newdcb->evq.next = NULL;
    newdcb->evq.prev = NULL;
    newdcb->evq.pending_events = 0;
    newdcb->evq.processing = 0;
    spinlock_init(&newdcb->evq.eventqlock);

    memset(&newdcb->stats, 0, sizeof(DCBSTATS));        // Zero the statistics
    newdcb->state = DCB_STATE_ALLOC;
    bitmask_init(&newdcb->memdata.bitmask);
    newdcb->writeqlen = 0;
    newdcb->high_water = 0;
    newdcb->low_water = 0;
    newdcb->session = NULL;
    newdcb->server = NULL;
    newdcb->service = NULL;
    newdcb->next = NULL;
    newdcb->nextpersistent = NULL;
    newdcb->persistentstart = 0;
    newdcb->callbacks = NULL;
    newdcb->data = NULL;

    newdcb->remote = NULL;
    newdcb->user = NULL;
    newdcb->flags = 0;

    spinlock_acquire(&dcbspin);
    if (allDCBs == NULL)
    {
        allDCBs = newdcb;
    }
    else
    {
        DCB *ptr = allDCBs;
        while (ptr->next)
            ptr = ptr->next;
        ptr->next = newdcb;
    }
    nDCBs++;
    if (nDCBs > maxDCBs)
    {
        maxDCBs = nDCBs;
    }
    spinlock_release(&dcbspin);
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
    DCB *clonedcb;

    if ((clonedcb = dcb_alloc(DCB_ROLE_REQUEST_HANDLER)))
    {
        clonedcb->fd = DCBFD_CLOSED;
        clonedcb->flags |= DCBF_CLONE;
        clonedcb->state = orig->state;
        clonedcb->data = orig->data;
        if (orig->remote)
        {
            clonedcb->remote = strdup(orig->remote);
        }
        if (orig->user)
        {
            clonedcb->user = strdup(orig->user);
        }
        clonedcb->protocol = orig->protocol;

        clonedcb->func.write = dcb_null_write;
        /**
         * Close triggers closing of router session as well which is needed.
         */
        clonedcb->func.close = orig->func.close;
        clonedcb->func.auth = dcb_null_auth;
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
    DCB_CALLBACK *cb;

    CHK_DCB(dcb);
    ss_info_dassert(dcb->state == DCB_STATE_DISCONNECTED ||
                    dcb->state == DCB_STATE_ALLOC,
                    "dcb not in DCB_STATE_DISCONNECTED not in DCB_STATE_ALLOC state.");

    if (DCB_POLL_BUSY(dcb))
    {
        /* Check if DCB has outstanding poll events */
        MXS_ERROR("dcb_final_free: DCB %p has outstanding events.", dcb);
    }

    /*< First remove this DCB from the chain */
    spinlock_acquire(&dcbspin);
    if (allDCBs == dcb)
    {
        /*<
         * Deal with the special case of removing the DCB at the head of
         * the chain.
         */
        allDCBs = dcb->next;
    }
    else
    {
        /*<
         * We find the DCB that point to the one we are removing and then
         * set the next pointer of that DCB to the next pointer of the
         * DCB we are removing.
         */
        DCB *ptr = allDCBs;
        while (ptr && ptr->next != dcb)
        {
            ptr = ptr->next;
        }
        if (ptr)
        {
            ptr->next = dcb->next;
        }
    }
    nDCBs--;
    spinlock_release(&dcbspin);

    if (dcb->session) {
        /*<
         * Terminate client session.
         */
        SESSION *local_session = dcb->session;
        dcb->session = NULL;
        CHK_SESSION(local_session);
        /**
         * Set session's client pointer NULL so that other threads
         * won't try to call dcb_close for client DCB
         * after this call.
         */
        if (local_session->client == dcb)
        {
            spinlock_acquire(&local_session->ses_lock);
            local_session->client = NULL;
            spinlock_release(&local_session->ses_lock);
        }
        if (SESSION_STATE_DUMMY != local_session->state)
        {
            session_free(local_session);
        }
    }

    if (dcb->protocol && (!DCB_IS_CLONE(dcb)))
    {
        free(dcb->protocol);
    }
    if (dcb->protoname)
    {
        free(dcb->protoname);
    }
    if (dcb->remote)
    {
        free(dcb->remote);
    }
    if (dcb->user)
    {
        free(dcb->user);
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

    spinlock_acquire(&dcb->cb_lock);
    while ((cb = dcb->callbacks) != NULL)
    {
        dcb->callbacks = cb->next;
        free(cb);
    }
    spinlock_release(&dcb->cb_lock);
    if (dcb->ssl)
    {
        SSL_free(dcb->ssl);
    }
    bitmask_free(&dcb->memdata.bitmask);
    free(dcb);
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
DCB *
dcb_process_zombies(int threadid)
{
    DCB *zombiedcb;
    DCB *previousdcb = NULL, *nextdcb;
    DCB *listofdcb = NULL;

    /**
     * Perform a dirty read to see if there is anything in the queue.
     * This avoids threads hitting the queue spinlock when the queue
     * is empty. This will really help when the only entry is being
     * freed, since the queue is updated before the expensive call to
     * dcb_final_free.
     */
    if (!zombies)
    {
        return NULL;
    }

    /*
     * Process the zombie queue and create a list of DCB's that can be
     * finally freed. This processing is down under a spinlock that
     * will prevent new entries being added to the zombie queue. Therefore
     * we do not want to do any expensive operations under this spinlock
     * as it will block other threads. The expensive operations will be
     * performed on the victim queue within holding the zombie queue
     * spinlock.
     */
    spinlock_acquire(&zombiespin);
    zombiedcb = zombies;
    while (zombiedcb)
    {
        CHK_DCB(zombiedcb);
        nextdcb = zombiedcb->memdata.next;
        /*
         * Skip processing of DCB's that are
         * in the event queue waiting to be processed.
         */
        if (zombiedcb->evq.next || zombiedcb->evq.prev)
        {
            previousdcb = zombiedcb;
        }
        else
        {

            if (bitmask_clear_without_spinlock(&zombiedcb->memdata.bitmask, threadid))
            {
                /**
                 * Remove the DCB from the zombie queue
                 * and call the final free routine for the
                 * DCB
                 *
                 * zombiedcb is the DCB we are processing
                 * previousdcb is the previous DCB on the zombie
                 * queue or NULL if the DCB is at the head of the
                 * queue.  Remove zombiedcb from the zombies list.
                 */
                if (NULL == previousdcb)
                {
                    zombies = zombiedcb->memdata.next;
                }
                else
                {
                    previousdcb->memdata.next = zombiedcb->memdata.next;
                }

                MXS_DEBUG("%lu [%s] Remove dcb "
                          "%p fd %d in state %s from the "
                          "list of zombies.",
                          pthread_self(),
                          __func__,
                          zombiedcb,
                          zombiedcb->fd,
                          STRDCBSTATE(zombiedcb->state));
                /*<
                 * Move zombie dcb to linked list of victim dcbs.
                 * The variable dcb is used to hold the last DCB
                 * to have been added to the linked list, or NULL
                 * if none has yet been added.  If the list
                 * (listofdcb) is not NULL, then it follows that
                 * dcb will also not be null.
                 */
                nzombies--;
                zombiedcb->memdata.next = listofdcb;
                listofdcb = zombiedcb;
            }
            else
            {
                /* Since we didn't remove this dcb from the zombies
                   list, we need to advance the previous pointer */
                previousdcb = zombiedcb;
            }
        }
        zombiedcb = nextdcb;
    }
    spinlock_release(&zombiespin);

    if (listofdcb)
    {
        dcb_process_victim_queue(listofdcb);
    }

    return zombies;
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
dcb_process_victim_queue(DCB *listofdcb)
{
    DCB *dcb = listofdcb;

    while (dcb != NULL)
    {
        DCB *nextdcb;
        /*<
         * Stop dcb's listening and modify state accordingly.
         */
        spinlock_acquire(&dcb->dcb_initlock);
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
                /* Must be DCB_STATE_POLLING */
                spinlock_release(&dcb->dcb_initlock);
                if (0 == dcb->persistentstart && dcb_maybe_add_persistent(dcb))
                {
                    /* Have taken DCB into persistent pool, no further killing */
                    dcb = dcb->memdata.next;
                    continue;
                }
                else
                {
                    DCB *nextdcb;
                    dcb_stop_polling_and_shutdown(dcb);
                    spinlock_acquire(&zombiespin);
                    bitmask_copy(&dcb->memdata.bitmask, poll_bitmask());
                    nextdcb = dcb->memdata.next;
                    dcb->memdata.next = zombies;
                    zombies = dcb;
                    nzombies++;
                    if (nzombies > maxzombies)
                    {
                        maxzombies = nzombies;
                    }
                    spinlock_release(&zombiespin);
                    dcb = nextdcb;
                    continue;
                }
            }
        }
        /*
         * Into the final close logic, so if DCB is for backend server, we
         * must decrement the number of current connections.
         */
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
                char errbuf[STRERROR_BUFLEN];
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
#if defined(FAKE_CODE)
                conn_open[dcb->fd] = false;
#endif /* FAKE_CODE */
                dcb->fd = DCBFD_CLOSED;

                MXS_DEBUG("%lu [dcb_process_victim_queue] Closed socket "
                          "%d on dcb %p.",
                          pthread_self(),
                          dcb->fd,
                          dcb);
            }
        }

        dcb_get_ses_log_info(dcb,
                             &mxs_log_tls.li_sesid,
                             &mxs_log_tls.li_enabled_priorities);

        dcb->state = DCB_STATE_DISCONNECTED;
        nextdcb = dcb->memdata.next;
        spinlock_release(&dcb->dcb_initlock);
        dcb_final_free(dcb);
        dcb = nextdcb;
    }
    /** Reset threads session data */
    mxs_log_tls.li_sesid = 0;
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
dcb_connect(SERVER *server, SESSION *session, const char *protocol)
{
    DCB         *dcb;
    GWPROTOCOL  *funcs;
    int         fd;
    int         rc;
    char        *user;

    user = session_getUser(session);
    if (user && strlen(user))
    {
        MXS_DEBUG("%lu [dcb_connect] Looking for persistent connection DCB "
                  "user %s protocol %s\n", pthread_self(), user, protocol);
        dcb = server_get_persistent(server, user, protocol);
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
            return dcb;
        }
        else
        {
            MXS_DEBUG("%lu [dcb_connect] Failed to find a reusable persistent connection.\n",
                      pthread_self());
        }
    }

    if ((dcb = dcb_alloc(DCB_ROLE_REQUEST_HANDLER)) == NULL)
    {
        return NULL;
    }

    if ((funcs = (GWPROTOCOL *)load_module(protocol,
                                           MODULE_PROTOCOL)) == NULL)
    {
        dcb->state = DCB_STATE_DISCONNECTED;
        dcb_final_free(dcb);
        MXS_ERROR("Failed to load protocol module for %s, free dcb %p\n",
                  protocol,
                  dcb);
        return NULL;
    }
    memcpy(&(dcb->func), funcs, sizeof(GWPROTOCOL));
    dcb->protoname = strdup(protocol);

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
        MXS_DEBUG("%lu [dcb_connect] Failed to connect to server %s:%d, "
                  "from backend dcb %p, client dcp %p fd %d.",
                  pthread_self(),
                  server->name,
                  server->port,
                  dcb,
                  session->client,
                  session->client->fd);
        dcb->state = DCB_STATE_DISCONNECTED;
        dcb_final_free(dcb);
        return NULL;
    }
    else
    {
        MXS_DEBUG("%lu [dcb_connect] Connected to server %s:%d, "
                  "from backend dcb %p, client dcp %p fd %d.",
                  pthread_self(),
                  server->name,
                  server->port,
                  dcb,
                  session->client,
                  session->client->fd);
    }
    /**
     * Successfully connected to backend. Assign file descriptor to dcb
     */
    dcb->fd = fd;

    /**
     * Add server pointer to dcb
     */
    dcb->server = server;

    /** Copy status field to DCB */
    dcb->dcb_server_status = server->status;
    dcb->dcb_port = server->port;

    /**
     * backend_dcb is connected to backend server, and once backend_dcb
     * is added to poll set, authentication takes place as part of
     * EPOLLOUT event that will be received once the connection
     * is established.
     */

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
static inline int
dcb_bytes_readable(DCB *dcb)
{
    int bytesavailable;

    if (-1 == ioctl(dcb->fd, FIONREAD, &bytesavailable))
    {
        char errbuf[STRERROR_BUFLEN];
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
static inline int
dcb_read_no_bytes_available(DCB *dcb, int nreadtotal)
{
    /** Handle closed client socket */
    if (nreadtotal == 0 && dcb_isclient(dcb))
    {
        char c;
        int l_errno = 0;
        int r = -1;

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
static inline GWBUF *
dcb_basic_read(DCB *dcb, int bytesavailable, int maxbytes, int nreadtotal, int *nsingleread)
{
    GWBUF *buffer;

    int bufsize = MIN(bytesavailable, MAX_BUFFER_SIZE);
    if (maxbytes)
    {
        bufsize = MIN(bufsize, maxbytes-nreadtotal);
    }

    if ((buffer = gwbuf_alloc(bufsize)) == NULL)
    {
        /*<
         * This is a fatal error which should cause shutdown.
         * Todo shutdown if memory allocation fails.
         */
        char errbuf[STRERROR_BUFLEN];
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
                char errbuf[STRERROR_BUFLEN];
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
int
dcb_read_SSL(DCB *dcb, GWBUF **head)
{
    GWBUF *buffer = NULL;
    int nsingleread = 0, nreadtotal = 0;

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

        while (buffer || SSL_pending(dcb->ssl))
        {
            dcb->last_read = hkheartbeat;
            buffer = dcb_basic_read_SSL(dcb, &nsingleread);
            if (NULL != buffer)
            {
                nreadtotal += nsingleread;
                /*< Append read data to the gwbuf */
                *head = gwbuf_append(*head, buffer);
            }
        }
    }

    ss_dassert(gwbuf_length(*head) == nreadtotal);
    MXS_DEBUG("%lu Read a total of %d bytes from dcb %p in state %s fd %d.",
              pthread_self(),
              nreadtotal,
              dcb,
              STRDCBSTATE(dcb->state),
              dcb->fd);

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
    unsigned char *temp_buffer[MAX_BUFFER_SIZE];
    GWBUF *buffer = NULL;

    *nsingleread = SSL_read(dcb->ssl, (void *)temp_buffer, MAX_BUFFER_SIZE);
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
            char errbuf[STRERROR_BUFLEN];
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
        spinlock_acquire(&dcb->writeqlock);
        /* If we were in a retry situation, need to clear flag and attempt write */
        if (dcb->ssl_read_want_write || dcb->ssl_read_want_read)
        {
            dcb->ssl_read_want_write = false;
            dcb->ssl_read_want_read = false;
            spinlock_release(&dcb->writeqlock);
            dcb_drain_writeq(dcb);
        }
        else
        {
            spinlock_release(&dcb->writeqlock);
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
        spinlock_acquire(&dcb->writeqlock);
        dcb->ssl_read_want_write = false;
        dcb->ssl_read_want_read = true;
        spinlock_release(&dcb->writeqlock);
        *nsingleread = 0;
        break;

    case SSL_ERROR_WANT_WRITE:
        /* Prevent SSL I/O on connection until retried, return to poll loop */
        MXS_DEBUG("%lu [%s] SSL connection want write",
                  pthread_self(),
                  __func__
                );
        spinlock_acquire(&dcb->writeqlock);
        dcb->ssl_read_want_write = true;
        dcb->ssl_read_want_read = false;
        spinlock_release(&dcb->writeqlock);
        *nsingleread = 0;
        break;

    case SSL_ERROR_SYSCALL:
        dcb_log_errors_SSL(dcb, __func__, *nsingleread);
        *nsingleread = -1;
        break;

    default:
        dcb_log_errors_SSL(dcb, __func__, 0);
        *nsingleread = -1;
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
 * @return          void
 */
static void
dcb_log_errors_SSL (DCB *dcb, const char *called_by, int ret)
{
    char errbuf[STRERROR_BUFLEN];
    unsigned long ssl_errno;

    MXS_ERROR("SSL operation failed in %s, dcb %p in state "
        "%s fd %d. More details may follow.",
        called_by,
        dcb,
        STRDCBSTATE(dcb->state),
        dcb->fd);

    ssl_errno = ERR_get_error();
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
            ERR_error_string_n(ssl_errno, errbuf, STRERROR_BUFLEN);
            MXS_ERROR("%s", errbuf);
            ssl_errno = ERR_get_error();
        }
    }
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
    int below_water;

    below_water = (dcb->high_water && dcb->writeqlen < dcb->high_water) ? 1 : 0;
    // The following guarantees that queue is not NULL
    if (!dcb_write_parameter_check(dcb, queue))
    {
        return 0;
    }

    spinlock_acquire(&dcb->writeqlock);
    empty_queue = (dcb->writeq == NULL);
    /*
     * Add our data to the write queue.  If the queue already had data,
     * then there will be an EPOLLOUT event to drain what is already queued.
     * If it did not already have data, we call the drain write queue
     * function immediately to attempt to write the data.
     */
    atomic_add(&dcb->writeqlen, gwbuf_length(queue));
    dcb->writeq = gwbuf_append(dcb->writeq, queue);
    spinlock_release(&dcb->writeqlock);
    dcb->stats.n_buffered++;
    MXS_DEBUG("%lu [dcb_write] Append to writequeue. %d writes "
              "buffered for dcb %p in state %s fd %d",
              pthread_self(),
              dcb->stats.n_buffered,
              dcb,
              STRDCBSTATE(dcb->state),
              dcb->fd);
    if (empty_queue) dcb_drain_writeq(dcb);
    dcb_write_tidy_up(dcb, below_water);

    return 1;
}

#if defined(FAKE_CODE)
/**
 * Fake code for dcb_write
 * (Should have fuller description)
 *
 * @param dcb   The DCB of the client
 */
static inline void
dcb_write_fake_code(DCB *dcb)
{
    if (dcb->dcb_role == DCB_ROLE_REQUEST_HANDLER && dcb->session != NULL)
    {
        if (dcb_isclient(dcb) && fail_next_client_fd)
        {
            dcb_fake_write_errno[dcb->fd] = 32;
            dcb_fake_write_ev[dcb->fd] = 29;
            fail_next_client_fd = false;
        }
        else if (!dcb_isclient(dcb) && fail_next_backend_fd)
        {
            dcb_fake_write_errno[dcb->fd] = 32;
            dcb_fake_write_ev[dcb->fd] = 29;
            fail_next_backend_fd = false;
        }
    }
}
#endif /* FAKE_CODE */

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
            char errbuf[STRERROR_BUFLEN];
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
            char errbuf[STRERROR_BUFLEN];
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
            char errbuf[STRERROR_BUFLEN];
            MXS_DEBUG("%lu [dcb_write] Writing to %s socket failed due %d, %s.",
                      pthread_self(),
                      dcb_isclient(dcb) ? "client" : "backend server",
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
    spinlock_release(&dcb->writeqlock);

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
    int written;
    bool stop_writing = false;
    bool above_water = (dcb->low_water && dcb->writeqlen > dcb->low_water);

    spinlock_acquire(&dcb->writeqlock);
    if (dcb->ssl_read_want_write)
    {
        poll_fake_event(dcb, EPOLLIN);
    }
    /*
     * Loop over the buffer chain in the pending writeq
     * Send as much of the data in that chain as possible and
     * leave any balance on the write queue.
     */
    while (dcb->writeq != NULL)
    {
        if (dcb->ssl)
        {
            written = gw_write_SSL(dcb, &stop_writing);
        }
        else
        {
            written = gw_write(dcb, &stop_writing);
        }
        if (stop_writing) break;
        /*
         * Pull the number of bytes we have written from
         * queue with have.
         */
        dcb->writeq = gwbuf_consume(dcb->writeq, written);
        MXS_DEBUG("%lu [dcb_drain_writeq] Wrote %d Bytes to dcb %p "
                  "in state %s fd %d",
                  pthread_self(),
                  written,
                  dcb,
                  STRDCBSTATE(dcb->state),
                  dcb->fd);
        total_written += written;
    }
    spinlock_release(&dcb->writeqlock);

    if (total_written)
    {
        atomic_add(&dcb->writeqlen, -total_written);
    }

    /* The write queue has drained, potentially need to call a callback function */
    if (dcb->writeq == NULL)
    {
        dcb_call_callback(dcb, DCB_REASON_DRAINED);
    }

    /* Check if the draining has taken us from above water to below water */
    if (above_water && dcb->writeqlen < dcb->low_water)
    {
        atomic_add(&dcb->stats.n_low_water, 1);
        dcb_call_callback(dcb, DCB_REASON_LOW_WATER);
    }

    return total_written;
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
        MXS_ERROR("%lu [dcb_close] Error : Removing DCB %p but was in state %s "
                  "which is not legal for a call to dcb_close. ",
                  pthread_self(),
                  dcb,
                  STRDCBSTATE(dcb->state));
        raise(SIGABRT);
    }

    /**
     * dcb_close may be called for freshly created dcb, in which case
     * it only needs to be freed.
     */
    if (dcb->state == DCB_STATE_ALLOC && dcb->fd == DCBFD_CLOSED)
    {
        dcb_final_free(dcb);
        return;
    }

    /*
     * If DCB is in persistent pool, mark it as an error and exit
     */
    if (dcb->persistentstart > 0)
    {
        dcb->dcb_errhandle_called = true;
        return;
    }

    spinlock_acquire(&zombiespin);
    if (!dcb->dcb_is_zombie)
    {
        if (0 == dcb->persistentstart && dcb->server && DCB_STATE_POLLING == dcb->state)
        {
            /* May be a candidate for persistence, so save user name */
            char *user;
            user = session_getUser(dcb->session);
            if (user && strlen(user) && !dcb->user)
            {
                dcb->user = strdup(user);
            }
        }
        /*<
         * Add closing dcb to the top of the list, setting zombie marker
         */
        dcb->dcb_is_zombie = true;
        dcb->memdata.next = zombies;
        zombies = dcb;
        nzombies++;
        if (nzombies > maxzombies) maxzombies = nzombies;
        /*< Set bit for each maxscale thread. This should be done before
         * the state is changed, so as to protect the DCB from premature
         * destruction. */
        if (dcb->server)
        {
            bitmask_copy(&dcb->memdata.bitmask, poll_bitmask());
        }
    }
    spinlock_release(&zombiespin);
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
    int  poolcount = -1;
    if (dcb->user != NULL
        && strlen(dcb->user)
        && dcb->server
        && dcb->server->persistpoolmax
        && (dcb->server->status & SERVER_RUNNING)
        && !dcb->dcb_errhandle_called
        && !(dcb->flags & DCBF_HUNG)
        && (poolcount = dcb_persistent_clean_count(dcb, false)) < dcb->server->persistpoolmax)
    {
        DCB_CALLBACK *loopcallback;
        MXS_DEBUG("%lu [dcb_maybe_add_persistent] Adding DCB to persistent pool, user %s.\n",
                  pthread_self(),
                  dcb->user);
        dcb->dcb_is_zombie = false;
        dcb->persistentstart = time(NULL);
        if (dcb->session)
            /*<
             * Terminate client session.
             */
        {
            SESSION *local_session = dcb->session;
            session_set_dummy(dcb);
            CHK_SESSION(local_session);
            if (SESSION_STATE_DUMMY != local_session->state)
            {
                session_free(local_session);
            }
        }
        spinlock_acquire(&dcb->cb_lock);
        while ((loopcallback = dcb->callbacks) != NULL)
        {
            dcb->callbacks = loopcallback->next;
            free(loopcallback);
        }
        spinlock_release(&dcb->cb_lock);
        spinlock_acquire(&dcb->server->persistlock);
        dcb->nextpersistent = dcb->server->persistent;
        dcb->server->persistent = dcb;
        spinlock_release(&dcb->server->persistlock);
        atomic_add(&dcb->server->stats.n_persistent, 1);
        atomic_add(&dcb->server->stats.n_current, -1);
        return true;
    }
    else
    {
        MXS_DEBUG("%lu [dcb_maybe_add_persistent] Not adding DCB %p to persistent pool, "
                  "user %s, max for pool %ld, error handle called %s, hung flag %s, "
                  "server status %d, pool count %d.\n",
                  pthread_self(),
                  dcb,
                  dcb->user ? dcb->user : "",
                  (dcb->server && dcb->server->persistpoolmax) ? dcb->server->persistpoolmax : 0,
                  dcb->dcb_errhandle_called ? "true" : "false",
                  (dcb->flags & DCBF_HUNG) ? "true" : "false",
                  dcb->server ? dcb->server->status : 0,
                  poolcount);
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
        printf("\tQueued write data:    %d\n",gwbuf_length(dcb->writeq));
    }
    char *statusname = server_status(dcb->server);
    if (statusname)
    {
        printf("\tServer status:            %s\n", statusname);
        free(statusname);
    }
    char *rolename = dcb_role_name(dcb);
    if (rolename)
    {
        printf("\tRole:                     %s\n", rolename);
        free(rolename);
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


/**
 * Diagnostic to print all DCB allocated in the system
 *
 */
void printAllDCBs()
{
    DCB *dcb;

    spinlock_acquire(&dcbspin);
    dcb = allDCBs;
    while (dcb)
    {
        printDCB(dcb);
        dcb = dcb->next;
    }
    spinlock_release(&dcbspin);
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
        free(statusname);
    }
    char *rolename = dcb_role_name(dcb);
    if (rolename)
    {
        dcb_printf(pdcb, "\tRole:                     %s\n", rolename);
        free(rolename);
    }
    if (!bitmask_isallclear(&dcb->memdata.bitmask))
    {
        char *bitmasktext = bitmask_render_readable(&dcb->memdata.bitmask);
        if (bitmasktext)
        {
            dcb_printf(pdcb, "\tBitMask:                %s\n", bitmasktext);
            free(bitmasktext);
        }
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
/**
 * Diagnostic to print all DCB allocated in the system
 *
 * @param       pdcb    DCB to print results to
 */
void
dprintAllDCBs(DCB *pdcb)
{
    DCB *dcb;

    spinlock_acquire(&dcbspin);
#if SPINLOCK_PROFILE
    dcb_printf(pdcb, "DCB List Spinlock Statistics:\n");
    spinlock_stats(&dcbspin, spin_reporter, pdcb);
    dcb_printf(pdcb, "Zombie Queue Lock Statistics:\n");
    spinlock_stats(&zombiespin, spin_reporter, pdcb);
#endif
    dcb = allDCBs;
    while (dcb)
    {
        dprintOneDCB(pdcb, dcb);
        dcb = dcb->next;
    }
    spinlock_release(&dcbspin);
}

/**
 * Diagnostic routine to print DCB data in a tabular form.
 *
 * @param       pdcb    DCB to print results to
 */
void
dListDCBs(DCB *pdcb)
{
    DCB *dcb;

    spinlock_acquire(&dcbspin);
    dcb = allDCBs;
    dcb_printf(pdcb, "Descriptor Control Blocks\n");
    dcb_printf(pdcb, "------------------+----------------------------+--------------------+----------\n");
    dcb_printf(pdcb, " %-16s | %-26s | %-18s | %s\n",
               "DCB", "State", "Service", "Remote");
    dcb_printf(pdcb, "------------------+----------------------------+--------------------+----------\n");
    while (dcb)
    {
        dcb_printf(pdcb, " %-16p | %-26s | %-18s | %s\n",
                   dcb, gw_dcb_state2string(dcb->state),
                   ((dcb->session && dcb->session->service) ? dcb->session->service->name : ""),
                   (dcb->remote ? dcb->remote : ""));
        dcb = dcb->next;
    }
    dcb_printf(pdcb, "------------------+----------------------------+--------------------+----------\n\n");
    spinlock_release(&dcbspin);
}

/**
 * Diagnostic routine to print client DCB data in a tabular form.
 *
 * @param       pdcb    DCB to print results to
 */
void
dListClients(DCB *pdcb)
{
    DCB *dcb;

    spinlock_acquire(&dcbspin);
    dcb = allDCBs;
    dcb_printf(pdcb, "Client Connections\n");
    dcb_printf(pdcb, "-----------------+------------------+----------------------+------------\n");
    dcb_printf(pdcb, " %-15s | %-16s | %-20s | %s\n",
               "Client", "DCB", "Service", "Session");
    dcb_printf(pdcb, "-----------------+------------------+----------------------+------------\n");
    while (dcb)
    {
        if (dcb_isclient(dcb) && dcb->dcb_role == DCB_ROLE_REQUEST_HANDLER)
        {
            dcb_printf(pdcb, " %-15s | %16p | %-20s | %10p\n",
                       (dcb->remote ? dcb->remote : ""),
                       dcb, (dcb->session->service ?
                             dcb->session->service->name : ""),
                       dcb->session);
        }
        dcb = dcb->next;
    }
    dcb_printf(pdcb, "-----------------+------------------+----------------------+------------\n\n");
    spinlock_release(&dcbspin);
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
    dcb_printf(pdcb, "\tOwning Session:     %p\n", dcb->session);
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
        free(statusname);
    }
    char *rolename = dcb_role_name(dcb);
    if (rolename)
    {
        dcb_printf(pdcb, "\tRole:                     %s\n", rolename);
        free(rolename);
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
#if SPINLOCK_PROFILE
    dcb_printf(pdcb, "\tInitlock Statistics:\n");
    spinlock_stats(&dcb->dcb_initlock, spin_reporter, pdcb);
    dcb_printf(pdcb, "\tWrite Queue Lock Statistics:\n");
    spinlock_stats(&dcb->writeqlock, spin_reporter, pdcb);
    dcb_printf(pdcb, "\tDelay Queue Lock Statistics:\n");
    spinlock_stats(&dcb->delayqlock, spin_reporter, pdcb);
    dcb_printf(pdcb, "\tPollin Lock Statistics:\n");
    spinlock_stats(&dcb->pollinlock, spin_reporter, pdcb);
    dcb_printf(pdcb, "\tPollout Lock Statistics:\n");
    spinlock_stats(&dcb->polloutlock, spin_reporter, pdcb);
    dcb_printf(pdcb, "\tCallback Lock Statistics:\n");
    spinlock_stats(&dcb->cb_lock, spin_reporter, pdcb);
#endif
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
gw_dcb_state2string(int state)
{
    switch(state) {
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
    vsnprintf(GWBUF_DATA(buf), 10240, fmt, args);
    va_end(args);

    buf->end = GWBUF_DATA(buf) + strlen(GWBUF_DATA(buf));
    dcb->func.write(dcb, buf);
}

/**
 * Determine the role that a DCB plays within a session.
 *
 * @param dcb
 * @return Non-zero if the DCB is the client of the session
 */
int
dcb_isclient(DCB *dcb)
{
    if (dcb->state != DCB_STATE_LISTENING && dcb->session)
    {
        if (dcb->session->client)
        {
            return (dcb->session && dcb == dcb->session->client);
        }
    }

    return 0;
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
 * @param stop_writing  Set to true if the caller should stop writing, false otherwise
 * @return              Number of written bytes
 */
static int
gw_write_SSL(DCB *dcb, bool *stop_writing)
{
    int written;
    char errbuf[STRERROR_BUFLEN];

    written = SSL_write(dcb->ssl, GWBUF_DATA(dcb->writeq), GWBUF_LENGTH(dcb->writeq));

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
        dcb_log_errors_SSL(dcb, __func__, written);
        poll_fake_hangup_event(dcb);
        break;

    default:
        /* Report error(s) and shutdown the connection */
        *stop_writing = true;
        dcb_log_errors_SSL(dcb, __func__, 0);
        poll_fake_hangup_event(dcb);
        break;
    }

    return written > 0 ? written : 0;
}

/**
 * Write data to a DCB. The data is taken from the DCB's write queue.
 *
 * @param dcb           The DCB to write buffer
 * @param stop_writing  Set to true if the caller should stop writing, false otherwise
 * @return              Number of written bytes
 */
static int
gw_write(DCB *dcb, bool *stop_writing)
{
    int written = 0;
    int fd = dcb->fd;
    size_t nbytes = GWBUF_LENGTH(dcb->writeq);
    void *buf = GWBUF_DATA(dcb->writeq);
    int saved_errno;

    errno = 0;

#if defined(FAKE_CODE)
    if (fd > 0 && dcb_fake_write_errno[fd] != 0)
    {
        ss_dassert(dcb_fake_write_ev[fd] != 0);
        written = write(fd, buf, nbytes/2); /*< leave peer to read missing bytes */

        if (written > 0)
        {
            written = -1;
            errno = dcb_fake_write_errno[fd];
        }
    }
    else if (fd > 0)
    {
        written = write(fd, buf, nbytes);
    }
#else
    if (fd > 0)
    {
        written = write(fd, buf, nbytes);
    }
#endif /* FAKE_CODE */

#if defined(SS_DEBUG_MYSQL)
    {
        size_t   len;
        uint8_t* packet = (uint8_t *)buf;
        char*    str;

        /** Print only MySQL packets */
        if (written > 5)
        {
            str = (char *)&packet[5];
            len      = packet[0];
            len     += 256*packet[1];
            len     += 256*256*packet[2];

            if (strncmp(str, "insert", 6) == 0 ||
                strncmp(str, "create", 6) == 0 ||
                strncmp(str, "drop", 4) == 0)
            {
                ss_dassert((dcb->dcb_server_status & (SERVER_RUNNING|SERVER_MASTER|SERVER_SLAVE))==(SERVER_RUNNING|SERVER_MASTER));
            }

            if (strncmp(str, "set autocommit", 14) == 0 && nbytes > 17)
            {
                char* s = (char *)calloc(1, nbytes+1);

                if (nbytes-5 > len)
                {
                    size_t len2 = packet[4+len];
                    len2 += 256*packet[4+len+1];
                    len2 += 256*256*packet[4+len+2];

                    char* str2 = (char *)&packet[4+len+5];
                    snprintf(s, 5+len+len2, "long %s %s", (char *)str, (char *)str2);
                }
                else
                {
                    snprintf(s, len, "%s", (char *)str);
                }
                MXS_INFO("%lu [gw_write] Wrote %d bytes : %s ",
                         pthread_self(),
                         w,
                         s);
                free(s);
            }
        }
    }
#endif
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
            char errbuf[STRERROR_BUFLEN];
            MXS_ERROR("Write to dcb %p "
              "in state %s fd %d failed due errno %d, %s",
              dcb,
              STRDCBSTATE(dcb->state),
              dcb->fd,
              saved_errno,
              strerror_r(saved_errno, errbuf, sizeof(errbuf)));
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
    DCB_CALLBACK *cb, *ptr;
    int          rval = 1;

    if ((ptr = (DCB_CALLBACK *)malloc(sizeof(DCB_CALLBACK))) == NULL)
    {
        return 0;
    }
    ptr->reason = reason;
    ptr->cb = callback;
    ptr->userdata = userdata;
    ptr->next = NULL;
    spinlock_acquire(&dcb->cb_lock);
    cb = dcb->callbacks;
    if (cb == NULL)
    {
        dcb->callbacks = ptr;
        spinlock_release(&dcb->cb_lock);
    }
    else
    {
        while (cb)
        {
            if (cb->reason == reason && cb->cb == callback &&
                cb->userdata == userdata)
            {
                free(ptr);
                spinlock_release(&dcb->cb_lock);
                return 0;
            }
            if (cb->next == NULL)
            {
                cb->next = ptr;
                break;
            }
            cb = cb->next;
        }
        spinlock_release(&dcb->cb_lock);
    }
    return rval;
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

    spinlock_acquire(&dcb->cb_lock);
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
                spinlock_release(&dcb->cb_lock);
                free(cb);
                rval = 1;
                break;
            }
            pcb = cb;
            cb = cb->next;
        }
    }
    if (!rval)
    {
        spinlock_release(&dcb->cb_lock);
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

    spinlock_acquire(&dcb->cb_lock);
    cb = dcb->callbacks;
    while (cb)
    {
        if (cb->reason == reason)
        {
            nextcb = cb->next;
            spinlock_release(&dcb->cb_lock);

            MXS_DEBUG("%lu [dcb_call_callback] %s",
                      pthread_self(),
                      STRDCBREASON(reason));

            cb->cb(dcb, reason, cb->userdata);
            spinlock_acquire(&dcb->cb_lock);
            cb = nextcb;
        }
        else
        {
            cb = cb->next;
        }
    }
    spinlock_release(&dcb->cb_lock);
}

/**
 * Check the passed DCB to ensure it is in the list of allDCBS
 *
 * @param       dcb     The DCB to check
 * @return      1 if the DCB is in the list, otherwise 0
 */
int
dcb_isvalid(DCB *dcb)
{
    int rval = 0;

    if (dcb)
    {
        spinlock_acquire(&dcbspin);
        rval = dcb_isvalid_nolock(dcb);
        spinlock_release(&dcbspin);
    }

    return rval;
}

/**
 * Find a DCB in the list of all DCB's
 *
 * @param dcb       The DCB to find
 * @return          A pointer to the DCB or NULL if not in the list
 */
static inline DCB *
dcb_find_in_list (DCB *dcb)
{
    DCB *ptr = NULL;
    if (dcb)
    {
        ptr = allDCBs;
        while (ptr && ptr != dcb)
        {
            ptr = ptr->next;
        }
    }
    return ptr;
}

/**
 * Check the passed DCB to ensure it is in the list of allDCBS.
 * Requires that the DCB list is already locked before call.
 *
 * @param       dcb     The DCB to check
 * @return      1 if the DCB is in the list, otherwise 0
 */
static inline int
dcb_isvalid_nolock(DCB *dcb)
{
    return (dcb == dcb_find_in_list(dcb));
}


/**
 * Get the next DCB in the list of all DCB's
 *
 * @param dcb           The current DCB
 * @return      The pointer to the next DCB or NULL if this is the last
 */
static DCB *
dcb_get_next(DCB *dcb)
{
    spinlock_acquire(&dcbspin);
    if (dcb) {
        dcb = dcb_isvalid_nolock(dcb) ? dcb->next : NULL;
    }
    else dcb = allDCBs;
    spinlock_release(&dcbspin);

    return dcb;
}

/**
 * Call all the callbacks on all DCB's that match the server and the reason given
 *
 * @param reason        The DCB_REASON that triggers the callback
 */
void
dcb_call_foreach(struct server* server, DCB_REASON reason)
{
    MXS_DEBUG("%lu [dcb_call_foreach]", pthread_self());

    switch (reason) {
    case DCB_REASON_DRAINED:
    case DCB_REASON_HIGH_WATER:
    case DCB_REASON_LOW_WATER:
    case DCB_REASON_ERROR:
    case DCB_REASON_HUP:
    case DCB_REASON_NOT_RESPONDING:
    {
        DCB *dcb;
        spinlock_acquire(&dcbspin);
        dcb = allDCBs;

        while (dcb != NULL)
        {
            spinlock_acquire(&dcb->dcb_initlock);
            if (dcb->state == DCB_STATE_POLLING && dcb->server &&
                strcmp(dcb->server->unique_name,server->unique_name) == 0)
            {
                dcb_call_callback(dcb, DCB_REASON_NOT_RESPONDING);
            }
            spinlock_release(&dcb->dcb_initlock);
            dcb = dcb->next;
        }
        spinlock_release(&dcbspin);
        break;
    }

    default:
        break;
    }
    return;
}

/**
 * Call all the callbacks on all DCB's that match the server and the reason given
 *
 * @param reason        The DCB_REASON that triggers the callback
 */
void
dcb_hangup_foreach(struct server* server)
{
    MXS_DEBUG("%lu [dcb_hangup_foreach]", pthread_self());

    DCB *dcb;
    spinlock_acquire(&dcbspin);
    dcb = allDCBs;

    while (dcb != NULL)
    {
        spinlock_acquire(&dcb->dcb_initlock);
        if (dcb->state == DCB_STATE_POLLING && dcb->server &&
            strcmp(dcb->server->unique_name,server->unique_name) == 0)
        {
            poll_fake_hangup_event(dcb);
        }
        spinlock_release(&dcb->dcb_initlock);
        dcb = dcb->next;
    }
    spinlock_release(&dcbspin);
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
 * Null protocol close operation for use by cloned DCB's.
 *
 * @param dcb           The DCB being closed.
 */
static int
dcb_null_close(DCB *dcb)
{
    return 0;
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
dcb_null_auth(DCB *dcb, SERVER *server, SESSION *session, GWBUF *buf)
{
    return 0;
}

/**
 * Check persistent pool for expiry or excess size and count
 *
 * @param dcb           The DCB being closed.
 * @param cleanall      Boolean, if true the whole pool is cleared for the
 *                      server related to the given DCB
 * @return              A count of the DCBs remaining in the pool
 */
int
dcb_persistent_clean_count(DCB *dcb, bool cleanall)
{
    int count = 0;
    if (dcb && dcb->server)
    {
        SERVER *server = dcb->server;
        DCB *previousdcb = NULL;
        DCB *persistentdcb, *nextdcb;
        DCB *disposals = NULL;

        CHK_SERVER(server);
        spinlock_acquire(&server->persistlock);
        persistentdcb = server->persistent;
        while (persistentdcb) {
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
                if (previousdcb) {
                    previousdcb->nextpersistent = nextdcb;
                }
                else
                {
                    server->persistent = nextdcb;
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
        server->persistmax = MAX(server->persistmax, count);
        spinlock_release(&server->persistlock);
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

/**
 * Return DCB counts optionally filtered by usage
 *
 * @param       usage   The usage of the DCB
 * @return      A count of DCBs in the desired state
 */
int
dcb_count_by_usage(DCB_USAGE usage)
{
    int rval = 0;
    DCB *ptr;

    spinlock_acquire(&dcbspin);
    ptr = allDCBs;
    while (ptr)
    {
        switch (usage)
        {
        case DCB_USAGE_CLIENT:
            if (dcb_isclient(ptr))
            {
                rval++;
            }
            break;
        case DCB_USAGE_LISTENER:
            if (ptr->state == DCB_STATE_LISTENING)
            {
                rval++;
            }
            break;
        case DCB_USAGE_BACKEND:
            if (dcb_isclient(ptr) == 0
                && ptr->dcb_role == DCB_ROLE_REQUEST_HANDLER)
            {
                rval++;
            }
            break;
        case DCB_USAGE_INTERNAL:
            if (ptr->dcb_role == DCB_ROLE_REQUEST_HANDLER)
            {
                rval++;
            }
            break;
        case DCB_USAGE_ZOMBIE:
            if (DCB_ISZOMBIE(ptr))
            {
                rval++;
            }
            break;
        case DCB_USAGE_ALL:
            rval++;
            break;
        }
        ptr = ptr->next;
    }
    spinlock_release(&dcbspin);
    return rval;
}

/**
 * Create the SSL structure for this DCB.
 * This function creates the SSL structure for the given SSL context.
 * This context should be the context of the service.
 * @param       dcb
 * @return      -1 on error, 0 otherwise.
 */
int dcb_create_SSL(DCB* dcb)
{
    if (serviceInitSSL(dcb->service) != 0)
    {
        return -1;
    }

    if ((dcb->ssl = SSL_new(dcb->service->ctx)) == NULL)
    {
        MXS_ERROR("Failed to initialize SSL for connection.");
        return -1;
    }

    if (SSL_set_fd(dcb->ssl,dcb->fd) == 0)
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
    int ssl_rval;

    ssl_rval = SSL_accept(dcb->ssl);
    switch (SSL_get_error(dcb->ssl, ssl_rval))
    {
        case SSL_ERROR_NONE:
            MXS_DEBUG("SSL_accept done for %s", dcb->remote);
            return 1;
            break;

        case SSL_ERROR_WANT_READ:
            MXS_DEBUG("SSL_accept ongoing want read for %s", dcb->remote);
            return 0;
            break;

        case SSL_ERROR_WANT_WRITE:
            MXS_DEBUG("SSL_accept ongoing want write for %s", dcb->remote);
            return 0;
            break;

        case SSL_ERROR_ZERO_RETURN:
            MXS_DEBUG("SSL error, shut down cleanly during SSL accept %s", dcb->remote);
            dcb_log_errors_SSL(dcb, __func__, 0);
            poll_fake_hangup_event(dcb);
            return 0;
            break;

        case SSL_ERROR_SYSCALL:
            MXS_DEBUG("SSL connection SSL_ERROR_SYSCALL error during accept %s", dcb->remote);
            dcb_log_errors_SSL(dcb, __func__, ssl_rval);
            poll_fake_hangup_event(dcb);
            return -1;
            break;

        default:
            MXS_DEBUG("SSL connection shut down with error during SSL accept %s", dcb->remote);
            dcb_log_errors_SSL(dcb, __func__, 0);
            poll_fake_hangup_event(dcb);
            return -1;
            break;
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

    ssl_rval = SSL_connect(dcb->ssl);
    switch (SSL_get_error(dcb->ssl, ssl_rval))
    {
        case SSL_ERROR_NONE:
            MXS_DEBUG("SSL_connect done for %s", dcb->remote);
            return 1;
            break;

        case SSL_ERROR_WANT_READ:
            MXS_DEBUG("SSL_connect ongoing want read for %s", dcb->remote);
            return 0;
            break;

        case SSL_ERROR_WANT_WRITE:
            MXS_DEBUG("SSL_connect ongoing want write for %s", dcb->remote);
            return 0;
            break;

        case SSL_ERROR_ZERO_RETURN:
            MXS_DEBUG("SSL error, shut down cleanly during SSL connect %s", dcb->remote);
            dcb_log_errors_SSL(dcb, __func__, 0);
            poll_fake_hangup_event(dcb);
            return 0;
            break;

        case SSL_ERROR_SYSCALL:
            MXS_DEBUG("SSL connection shut down with SSL_ERROR_SYSCALL during SSL connect %s", dcb->remote);
            dcb_log_errors_SSL(dcb, __func__, ssl_rval);
            poll_fake_hangup_event(dcb);
            return -1;
            break;

        default:
            MXS_DEBUG("SSL connection shut down with error during SSL connect %s", dcb->remote);
            dcb_log_errors_SSL(dcb, __func__, 0);
            poll_fake_hangup_event(dcb);
            return -1;
            break;
    }
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
    char *name = NULL;

    if (NULL != (name = (char *)malloc(64)))
    {
        name[0] = 0;
        if (DCB_ROLE_SERVICE_LISTENER == dcb->dcb_role)
        {
            strcat(name, "Service Listener");
        }
        else if (DCB_ROLE_REQUEST_HANDLER == dcb->dcb_role)
        {
            strcat(name, "Request Handler");
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
