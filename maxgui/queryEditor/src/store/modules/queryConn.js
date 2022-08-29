/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import queryHelper from './queryHelper'

const statesToBeSynced = queryHelper.syncStateCreator('queryConn')
const memStates = queryHelper.memStateCreator('queryConn')

export default {
    namespaced: true,
    state: {
        sql_conns: {}, // persisted
        is_validating_conn: true,
        conn_err_state: false,
        rc_target_names_map: {},
        pre_select_conn_rsrc: null,
        ...memStates,
        ...statesToBeSynced,
    },
    mutations: {
        ...queryHelper.memStatesMutationCreator(memStates),
        ...queryHelper.syncedStateMutationsCreator({
            statesToBeSynced,
            persistedArrayPath: 'querySession.query_sessions',
        }),
        SET_IS_VALIDATING_CONN(state, payload) {
            state.is_validating_conn = payload
        },
        SET_CONN_ERR_STATE(state, payload) {
            state.conn_err_state = payload
        },
        SET_SQL_CONNS(state, payload) {
            state.sql_conns = payload
        },
        ADD_SQL_CONN(state, payload) {
            this.vue.$set(state.sql_conns, payload.id, payload)
        },
        UPDATE_SQL_CONN(state, payload) {
            state.sql_conns = this.vue.$helpers.immutableUpdate(state.sql_conns, {
                [payload.id]: { $set: payload },
            })
        },
        DELETE_SQL_CONN(state, payload) {
            this.vue.$delete(state.sql_conns, payload.id)
        },
        SET_RC_TARGET_NAMES_MAP(state, payload) {
            state.rc_target_names_map = payload
        },
        SET_PRE_SELECT_CONN_RSRC(state, payload) {
            state.pre_select_conn_rsrc = payload
        },
    },
    actions: {
        async fetchRcTargetNames({ state, commit }, resourceType) {
            try {
                const res = await this.vue.$queryHttp.get(
                    `/${resourceType}?fields[${resourceType}]=id`
                )
                if (res.data.data) {
                    const names = res.data.data.map(({ id, type }) => ({ id, type }))
                    commit('SET_RC_TARGET_NAMES_MAP', {
                        ...state.rc_target_names_map,
                        [resourceType]: names,
                    })
                }
            } catch (e) {
                this.vue.$logger('store-queryConn-fetchRcTargetNames').error(e)
            }
        },
        /**
         * @param {Boolean} param.silentValidation - silent validation (without calling SET_IS_VALIDATING_CONN)
         */
        async validatingConn(
            { state, commit, dispatch, rootState, rootGetters },
            { silentValidation = false } = {}
        ) {
            if (!silentValidation) commit('SET_IS_VALIDATING_CONN', true)
            try {
                const active_session_id = rootGetters['querySession/getActiveSessionId']
                const active_session = rootGetters['querySession/getActiveSession']
                const res = await this.vue.$queryHttp.get(`/sql/`)
                const resConnMap = this.vue.$helpers.lodash.keyBy(res.data.data, 'id')
                const resConnIds = Object.keys(resConnMap)
                const clientConnIds = queryHelper.getClientConnIds()
                if (resConnIds.length === 0) {
                    dispatch('resetAllStates')
                    commit('SET_SQL_CONNS', {})
                } else {
                    const validConnIds = clientConnIds.filter(id => resConnIds.includes(id))
                    const validSqlConns = Object.keys(state.sql_conns)
                        .filter(id => validConnIds.includes(id))
                        .reduce(
                            (acc, id) => ({
                                ...acc,
                                [id]: {
                                    ...state.sql_conns[id],
                                    attributes: resConnMap[id].attributes, // update attributes
                                },
                            }),
                            {}
                        )
                    const invalidCnctIds = Object.keys(state.sql_conns).filter(
                        id => !(id in validSqlConns)
                    )
                    //delete cookies and reset sessions bound those invalid connections
                    invalidCnctIds.forEach(id => {
                        this.vue.$helpers.deleteCookie(`conn_id_body_${id}`)
                        dispatch('querySession/resetSessionStates', { conn_id: id }, { root: true })
                    })

                    // Delete also leftover cloned connections when its wke connections are expired
                    const wkeConnIds = Object.values(validSqlConns)
                        .filter(
                            c =>
                                c.binding_type ===
                                rootState.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES
                                    .WORKSHEET
                        )
                        .map(c => c.id)

                    const leftoverConns = Object.values(validSqlConns).filter(
                        c =>
                            !wkeConnIds.includes(c.clone_of_conn_id) &&
                            c.binding_type ===
                                rootState.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES.SESSION
                    )

                    let wkeIdsToBeReset = []
                    for (const conn of leftoverConns) {
                        await this.vue.$queryHttp.delete(`/sql/${conn.id}`)
                        const { wke_id_fk } = rootGetters['querySession/getSessionByConnId'](
                            conn.id
                        )
                        if (wke_id_fk && !wkeIdsToBeReset.includes(wke_id_fk))
                            wkeIdsToBeReset.push(wke_id_fk)
                        dispatch(
                            'querySession/resetSessionStates',
                            { conn_id: conn.id },
                            { root: true }
                        )
                    }

                    wkeIdsToBeReset.forEach(id => {
                        commit(
                            'wke/REFRESH_WKE',
                            rootState.wke.worksheets_arr.find(wke => wke.id === id),
                            { root: true }
                        )
                        dispatch('wke/releaseQueryModulesMem', id, { root: true })
                    })

                    // remove leftover conns
                    leftoverConns.forEach(c => delete validSqlConns[c.id])
                    commit('SET_SQL_CONNS', validSqlConns)
                    // get the connection of the active session
                    const session_conn = this.vue.$typy(active_session, 'active_sql_conn')
                        .safeObjectOrEmpty
                    // get session_conn new value after validating
                    const session_conn_updated = validSqlConns[session_conn.id] || {}
                    // update active_sql_conn attributes
                    if (session_conn_updated.id !== state.active_sql_conn.id) {
                        commit('SET_ACTIVE_SQL_CONN', {
                            payload: session_conn_updated,
                            id: active_session_id,
                        })
                    }
                }
            } catch (e) {
                this.vue.$logger('store-queryConn-validatingConn').error(e)
            }
            if (!silentValidation) commit('SET_IS_VALIDATING_CONN', false)
        },
        /**
         * Called by <conn-man-ctr/>
         * @param {Object} param.body - request body
         * @param {String} param.resourceType - services, servers or listeners.
         */
        async openConnect({ dispatch, commit, rootState, rootGetters }, { body, resourceType }) {
            // activeWkeSessions length always >=1 as the default session will be always created on startup
            const activeWkeSessions = rootGetters['querySession/getSessionsOfActiveWke']
            const active_session_id = rootGetters['querySession/getActiveSessionId']
            try {
                // create the connection
                const res = await this.vue.$queryHttp.post(`/sql?persist=yes&max-age=86400`, body)
                if (res.status === 201) {
                    commit(
                        'appNotifier/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.i18n.t('info.connSuccessfully')],
                            type: 'success',
                        },
                        { root: true }
                    )
                    const connId = res.data.data.id
                    const sql_conn = {
                        id: connId,
                        attributes: res.data.data.attributes,
                        name: body.target,
                        type: resourceType,
                        binding_type:
                            rootState.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES.WORKSHEET,
                        wke_id_fk: rootState.wke.active_wke_id,
                    }
                    commit('ADD_SQL_CONN', sql_conn)

                    let activeSessConn = sql_conn
                    if (activeWkeSessions.length)
                        await dispatch('cloneAndSyncConnToSessions', {
                            sessions: activeWkeSessions,
                            conn_to_be_cloned: sql_conn,
                            active_session_id,
                            getActiveSessConn: sessConn => (activeSessConn = sessConn),
                        })

                    /* To avoid concurrent query, only set the connection as active to the active
                     * session tab once cloned connections are bound to sessions
                     */
                    commit('SET_ACTIVE_SQL_CONN', {
                        id: active_session_id,
                        payload: activeSessConn,
                    })

                    if (body.db) await dispatch('useDb', body.db)
                    commit('SET_CONN_ERR_STATE', false)
                }
            } catch (e) {
                this.vue.$logger('store-queryConn-openConnect').error(e)
                commit('SET_CONN_ERR_STATE', true)
            }
        },
        /**
         * Sync the conn to the session in the persisted query_sessions array
         * @param {Object} param.sess - session object
         * @param {Object} param.sql_conn - connection object
         */
        syncSqlConnToSess(_, { sess, sql_conn }) {
            queryHelper.syncToPersistedObj({
                scope: this,
                data: { active_sql_conn: sql_conn },
                id: sess.id,
                persistedArrayPath: 'querySession.query_sessions',
            })
        },
        /**
         * This clones the provided connection and sync it to the session tab object
         * in query_sessions persisted array. If the session id === active_session_id,
         * it calls getActiveSessConn to get the clone connection to be bound to the
         * active session
         * @param {Array} param.sessions - sessions
         * @param {Object} param.conn_to_be_cloned - connection object to be cloned
         * @param {String} param.active_session_id - active_session_id
         * @param {Function} param.getActiveSessConn - callback function
         */
        async cloneAndSyncConnToSessions(
            { dispatch, rootState },
            { sessions, conn_to_be_cloned, active_session_id, getActiveSessConn }
        ) {
            // clone the connection and bind it to all other session tabs
            for (const s of sessions) {
                let sessConn
                await dispatch('cloneConn', {
                    conn_to_be_cloned,
                    binding_type:
                        rootState.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES.SESSION,
                    session_id_fk: s.id,
                    getCloneObjRes: obj => (sessConn = obj),
                })
                // return the connection for the active session
                if (s.id === active_session_id) getActiveSessConn(sessConn)
                // just sync the conn to the session
                else dispatch('syncSqlConnToSess', { sess: s, sql_conn: sessConn })
            }
        },
        /**
         *  Clone a connection
         * @param {Object} param.conn_to_be_cloned - connection to be cloned
         * @param {String} param.binding_type - binding_type. Check QUERY_CONN_BINDING_TYPES
         * @param {String} param.session_id_fk - id of the session that binds this connection
         * @param {Function} param.getCloneObjRes - get the result of the clone object
         */
        async cloneConn(
            { commit },
            { conn_to_be_cloned, binding_type, session_id_fk, getCloneObjRes }
        ) {
            try {
                const res = await this.vue.$queryHttp.post(
                    `/sql/${conn_to_be_cloned.id}/clone?persist=yes&max-age=86400`
                )
                if (res.status === 201) {
                    const connId = res.data.data.id
                    const conn = {
                        id: connId,
                        attributes: res.data.data.attributes,
                        name: conn_to_be_cloned.name,
                        type: conn_to_be_cloned.type,
                        clone_of_conn_id: conn_to_be_cloned.id,
                        binding_type,
                        session_id_fk,
                    }
                    this.vue.$typy(getCloneObjRes).safeFunction(conn)
                    commit('ADD_SQL_CONN', conn)
                }
            } catch (e) {
                this.vue.$logger('store-queryConn-cloneConn').error(e)
            }
        },
        /**
         * This handles deleting a clone connection.
         * @param {Number} param.id - connection id to be deleted
         */
        async disconnectClone({ state, commit }, { id }) {
            try {
                const res = await this.vue.$queryHttp.delete(`/sql/${id}`)
                if (res.status === 204) commit('DELETE_SQL_CONN', state.sql_conns[id])
            } catch (e) {
                this.vue.$logger('store-queryConn-disconnectClone').error(e)
            }
        },
        /**
         * This handles delete the worksheet connection. i.e. the
         * connection created by the user in the <conn-man-ctr/>
         * It will also delete its cloned connections by using `clone_of_conn_id` attribute.
         * This action is meant to be used by:
         * `conn-man-ctr` component to disconnect a resource connection
         * `disconnectAll` action to delete all connection when leaving the page.
         * `handleDeleteWke` action
         * @param {Boolean} param.showSnackbar - should show success message or not
         * @param {Number} param.id - connection id that is bound to the first session tab
         */
        async disconnect({ state, commit, dispatch }, { showSnackbar, id: wkeConnId }) {
            try {
                if (state.sql_conns[wkeConnId]) {
                    const clonedConnIds = Object.values(state.sql_conns)
                        .filter(c => c.clone_of_conn_id === wkeConnId)
                        .map(c => c.id)
                    const cnnIdsToBeDeleted = [wkeConnId, ...clonedConnIds]
                    dispatch('wke/resetWkeStates', wkeConnId, { root: true })

                    const allRes = await Promise.all(
                        cnnIdsToBeDeleted.map(id => {
                            commit('DELETE_SQL_CONN', state.sql_conns[id])
                            dispatch(
                                'querySession/resetSessionStates',
                                { conn_id: id },
                                { root: true }
                            )
                            return this.vue.$queryHttp.delete(`/sql/${id}`)
                        })
                    )

                    if (allRes.every(promise => promise.status === 204) && showSnackbar)
                        commit(
                            'appNotifier/SET_SNACK_BAR_MESSAGE',
                            {
                                text: [this.i18n.t('info.disconnSuccessfully')],
                                type: 'success',
                            },
                            { root: true }
                        )
                }
            } catch (e) {
                this.vue.$logger('store-queryConn-disconnect').error(e)
            }
        },
        async disconnectAll({ getters, dispatch }) {
            try {
                for (const { id = '' } of getters.getWkeConns)
                    await dispatch('disconnect', { showSnackbar: false, id })
            } catch (e) {
                this.vue.$logger('store-queryConn-disconnectAll').error(e)
            }
        },
        async reconnect({ state, commit, dispatch }) {
            const active_sql_conn = state.active_sql_conn
            try {
                const res = await this.vue.$queryHttp.post(`/sql/${active_sql_conn.id}/reconnect`)
                if (res.status === 204) {
                    commit(
                        'appNotifier/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.i18n.t('info.reconnSuccessfully')],
                            type: 'success',
                        },
                        { root: true }
                    )
                    await dispatch('schemaSidebar/initialFetch', active_sql_conn, {
                        root: true,
                    })
                } else
                    commit(
                        'appNotifier/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.i18n.t('errors.reconnFailed')],
                            type: 'error',
                        },
                        { root: true }
                    )
                await dispatch('validatingConn', { silentValidation: true })
            } catch (e) {
                this.vue.$logger('store-queryConn-reconnect').error(e)
            }
        },
        clearConn({ commit, dispatch, state }) {
            try {
                const active_sql_conn = state.active_sql_conn
                dispatch(
                    'querySession/resetSessionStates',
                    { conn_id: active_sql_conn.id },
                    { root: true }
                )
                commit('DELETE_SQL_CONN', active_sql_conn)
            } catch (e) {
                this.vue.$logger('store-queryConn-clearConn').error(e)
            }
        },

        async updateActiveDb({ state, commit, rootGetters }) {
            const { active_sql_conn, active_db } = state
            const active_session_id = rootGetters['querySession/getActiveSessionId']
            try {
                let res = await this.vue.$queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
                    sql: 'SELECT DATABASE()',
                })
                const resActiveDb = this.vue
                    .$typy(res, 'data.data.attributes.results[0].data')
                    .safeArray.flat()[0]
                if (!resActiveDb) commit('SET_ACTIVE_DB', { payload: '', id: active_session_id })
                else if (active_db !== resActiveDb)
                    commit('SET_ACTIVE_DB', { payload: resActiveDb, id: active_session_id })
            } catch (e) {
                this.vue.$logger('store-queryConn-updateActiveDb').error(e)
            }
        },

        /**
         * @param {String} db - database name
         */
        async useDb({ commit, dispatch, state, rootState, rootGetters }, db) {
            const active_sql_conn = state.active_sql_conn
            const active_session_id = rootGetters['querySession/getActiveSessionId']
            try {
                const now = new Date().valueOf()
                const escapedDb = this.vue.$helpers.escapeIdentifiers(db)
                const sql = `USE ${escapedDb};`
                let res = await this.vue.$queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
                    sql,
                })
                let queryName = `Change default database to ${escapedDb}`
                if (res.data.data.attributes.results[0].errno) {
                    const errObj = res.data.data.attributes.results[0]
                    commit(
                        'appNotifier/SET_SNACK_BAR_MESSAGE',
                        {
                            text: Object.keys(errObj).map(key => `${key}: ${errObj[key]}`),
                            type: 'error',
                        },
                        { root: true }
                    )
                    queryName = `Failed to change default database to ${escapedDb}`
                } else commit('SET_ACTIVE_DB', { payload: db, id: active_session_id })
                dispatch(
                    'queryPersisted/pushQueryLog',
                    {
                        startTime: now,
                        name: queryName,
                        sql,
                        res,
                        connection_name: active_sql_conn.name,
                        queryType: rootState.queryEditorConfig.config.QUERY_LOG_TYPES.ACTION_LOGS,
                    },
                    { root: true }
                )
            } catch (e) {
                this.vue.$logger('store-queryConn-useDb').error(e)
            }
        },

        // Reset all when there is no active connections
        resetAllStates({ rootState, rootGetters, commit }) {
            for (const targetWke of rootState.wke.worksheets_arr) {
                commit('wke/REFRESH_WKE', targetWke, { root: true })
                const sessions = rootGetters['querySession/getSessionsByWkeId'](targetWke.id)
                for (const s of sessions)
                    commit('querySession/REFRESH_SESSION_OF_A_WKE', s, { root: true })
            }
        },
    },
    getters: {
        getBgConn: (state, getters, rootState) => {
            /**
             * @param {Number} - active connection id that was cloned
             */
            return ({ session_id_fk }) =>
                Object.values(state.sql_conns).find(
                    conn =>
                        conn.binding_type ===
                            rootState.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES
                                .BACKGROUND && conn.session_id_fk === session_id_fk
                ) || {}
        },
        getIsConnBusy: (state, getters, rootState, rootGetters) => {
            const { value = false } =
                state.is_conn_busy_map[rootGetters['querySession/getActiveSessionId']] || {}
            return value
        },
        getIsConnBusyBySessionId: state => {
            return session_id => {
                const { value = false } = state.is_conn_busy_map[session_id] || {}
                return value
            }
        },
        getLostCnnErrMsgObj: (state, getters, rootState, rootGetters) => {
            const { value = {} } =
                state.lost_cnn_err_msg_obj_map[rootGetters['querySession/getActiveSessionId']] || {}
            return value
        },
        getWkeConns: (state, getters, rootState) =>
            Object.values(state.sql_conns).filter(
                c =>
                    c.binding_type ===
                    rootState.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES.WORKSHEET
            ) || [],

        getClonedConnsOfWkeConn: state => wkeConnId =>
            Object.values(state.sql_conns).filter(c => c.clone_of_conn_id === wkeConnId) || [],

        getCurrWkeConn: (state, getters, rootState) =>
            getters.getWkeConns.find(c => c.wke_id_fk === rootState.wke.active_wke_id) || {},

        getWkeConnByWkeId: (state, getters) => wke_id =>
            getters.getWkeConns.find(c => c.wke_id_fk === wke_id) || {},
    },
}
