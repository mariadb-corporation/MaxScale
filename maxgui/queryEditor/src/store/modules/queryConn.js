/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
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
        /**
         * alive_conn_map: stores connections that exists in the response of a GET to /sql/
         * and in the `fetchConnStatus` action argument sqlConns
         * orphaned_conns: When wke connection expires but its cloned connections (query tabs)
         * are still alive, those are orphaned connections
         */
        conn_status_data: { alive_conn_map: {}, expired_conn_map: {}, orphaned_conns: [] },
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
        SET_CONN_STATUS_DATA(state, payload) {
            state.conn_status_data = payload
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
         * @param {Array} conns - connections that were cloned from expired worksheet connections
         */
        async cleanUpOrphanedConns({ commit, dispatch, rootState, rootGetters }, conns) {
            try {
                let wkeIdsToBeReset = []
                for (const conn of conns) {
                    await this.vue.$queryHttp.delete(`/sql/${conn.id}`)
                    const { wke_id_fk } = rootGetters['querySession/getSessionByConnId'](conn.id)
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
            } catch (e) {
                this.vue.$logger('store-queryConn-cleanUpOrphanedConns').error(e)
            }
        },
        /**
         * update active_sql_conn attributes of the active session
         * @param {*} param0
         * @param {*} aliveConn
         */
        updateAliveActiveConn({ state, commit, rootGetters }, aliveActiveConn) {
            const active_session_id = rootGetters['querySession/getActiveSessionId']
            if (!this.vue.$helpers.lodash.isEqual(aliveActiveConn, state.active_sql_conn)) {
                commit('SET_ACTIVE_SQL_CONN', {
                    payload: aliveActiveConn,
                    id: active_session_id,
                })
            }
        },
        /**
         * This action has side effects, it also cleans up orphaned connections and
         * reset query tabs (sessions) states for expired connections
         * @param {Object} sqlConns - sql connections
         */
        async fetchConnStatus({ commit }, sqlConns) {
            try {
                const res = await this.vue.$queryHttp.get(`/sql/`)
                const aliveConnMap = this.vue.$helpers.lodash.keyBy(res.data.data, 'id')

                let alive_conn_map = {},
                    expired_conn_map = {},
                    orphaned_conns = []

                if (!this.vue.$typy(aliveConnMap).isEmptyObject) {
                    Object.values(sqlConns).forEach(conn => {
                        const connId = conn.id
                        if (aliveConnMap[connId]) {
                            // if this has value, it is a cloned connection from the wke connection
                            const wkeConnId = this.vue.$typy(conn, 'clone_of_conn_id').safeString
                            if (wkeConnId && !aliveConnMap[wkeConnId]) orphaned_conns.push(conn)
                            else
                                alive_conn_map[connId] = {
                                    ...conn,
                                    // update attributes
                                    attributes: aliveConnMap[connId].attributes,
                                }
                        } else expired_conn_map[connId] = conn
                    })
                }
                commit('SET_CONN_STATUS_DATA', { alive_conn_map, expired_conn_map, orphaned_conns })
            } catch (e) {
                this.vue.$logger('store-queryConn-fetchConnStatus').error(e)
            }
        },
        /**
         * Validate provided sqlConns
         * @param {Object} param.sqlConns - sql connections stored in indexedDB
         * @param {Boolean} param.silentValidation - silent validation (without calling SET_IS_VALIDATING_CONN)
         * @param {Function} param.customSetSqlConns - custom function for SET_SQL_CONNS mutation
         */
        async validateConns(
            { state, commit, dispatch },
            { sqlConns, silentValidation = false, customSetSqlConns }
        ) {
            let aliveConnMap = {}
            if (!silentValidation) commit('SET_IS_VALIDATING_CONN', true)
            try {
                await dispatch('fetchConnStatus', sqlConns)

                if (this.vue.$typy(state.conn_status_data, 'alive_conn_map').isEmptyObject)
                    dispatch('resetAllStates')
                else {
                    const {
                        alive_conn_map = {},
                        expired_conn_map = {},
                        orphaned_conns = [],
                    } = state.conn_status_data
                    //reset sessions that are bound to those expired connections
                    Object.keys(expired_conn_map).forEach(id => {
                        dispatch('querySession/resetSessionStates', { conn_id: id }, { root: true })
                    })
                    await dispatch('cleanUpOrphanedConns', orphaned_conns)

                    const aliveActiveConn = alive_conn_map[state.active_sql_conn.id] || {}
                    await dispatch('updateAliveActiveConn', aliveActiveConn)
                    aliveConnMap = alive_conn_map
                }
            } catch (e) {
                this.vue.$logger('store-queryConn-validateConns').error(e)
            }
            if (this.vue.$typy(customSetSqlConns).isFunction) customSetSqlConns(aliveConnMap)
            else commit('SET_SQL_CONNS', aliveConnMap)
            commit('SET_IS_VALIDATING_CONN', false)
        },

        // Unbind the connection before opening/selecting new one, so that it can be used by other wke
        unbindConn({ commit, rootState, getters }) {
            const currentBoundConn = getters.getWkeConns.find(
                c => c.wke_id_fk === rootState.wke.active_wke_id
            )
            if (currentBoundConn) commit('UPDATE_SQL_CONN', { ...currentBoundConn, wke_id_fk: '' })
        },
        // bind the chosenWkeConn
        bindConn({ commit, rootState }, chosenWkeConn) {
            commit('UPDATE_SQL_CONN', { ...chosenWkeConn, wke_id_fk: rootState.wke.active_wke_id })
        },
        async onChangeConn({ commit, getters, dispatch, rootGetters }, chosenWkeConn) {
            try {
                dispatch('unbindConn')
                let activeSessConn = {}
                const active_session_id = rootGetters['querySession/getActiveSessionId']
                // Change the connection of all session tabs of the worksheet
                const sessions = rootGetters['querySession/getSessionsOfActiveWke']
                if (sessions.length) {
                    const clonesOfChosenWkeConn = getters.getClonedConnsOfWkeConn(chosenWkeConn.id)
                    const bondableSessTabs = sessions.slice(0, clonesOfChosenWkeConn.length)
                    const leftoverSessTabs = sessions.slice(clonesOfChosenWkeConn.length)
                    // Bind the existing cloned connections
                    for (const [i, s] of bondableSessTabs.entries()) {
                        // Bind the session connection with session_id_fk
                        commit('UPDATE_SQL_CONN', {
                            ...clonesOfChosenWkeConn[i],
                            session_id_fk: s.id,
                        })
                        dispatch('syncSqlConnToSess', {
                            sess: s,
                            sql_conn: clonesOfChosenWkeConn[i],
                        })
                        if (active_session_id === s.id) activeSessConn = clonesOfChosenWkeConn[i]
                    }
                    // clones the chosenWkeConn and bind them to leftover session tabs
                    await dispatch('cloneAndSyncConnToSessions', {
                        sessions: leftoverSessTabs,
                        conn_to_be_cloned: chosenWkeConn,
                        active_session_id,
                        getActiveSessConn: sessConn => (activeSessConn = sessConn),
                    })
                }
                // set active conn once sessions are bound to cloned connections to avoid concurrent query
                commit('SET_ACTIVE_SQL_CONN', { payload: activeSessConn, id: active_session_id })
                // bind the chosenWkeConn
                dispatch('bindConn', chosenWkeConn)
            } catch (e) {
                this.vue.$logger('store-queryConn-onChangeConn').error(e)
            }
        },
        /**
         * Called by <conn-man-ctr/>
         * @param {Object} param.body - request body
         * @param {String} param.resourceType - services, servers or listeners.
         * @param {Object} param.meta - meta - connection meta
         */
        async openConnect(
            { dispatch, commit, rootState, rootGetters },
            { body, resourceType, meta = {} }
        ) {
            // activeWkeSessions length always >=1 as the default session will be always created on startup
            const activeWkeSessions = rootGetters['querySession/getSessionsOfActiveWke']
            const active_session_id = rootGetters['querySession/getActiveSessionId']
            try {
                // create the connection
                const res = await this.vue.$queryHttp.post(
                    `/sql?persist=yes&max-age=${rootState.queryEditorConfig.auth_cookies_max_age}`,
                    body
                )
                if (res.status === 201) {
                    dispatch('unbindConn')
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.vue.$mxs_t('info.connSuccessfully')],
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
                        meta,
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
            { commit, rootState },
            { conn_to_be_cloned, binding_type, session_id_fk, getCloneObjRes }
        ) {
            try {
                const max_age = rootState.queryEditorConfig.auth_cookies_max_age
                const res = await this.vue.$queryHttp.post(
                    `/sql/${conn_to_be_cloned.id}/clone?persist=yes&max-age=${max_age}`
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
                        meta: this.vue.$typy(conn_to_be_cloned, 'meta').safeObjectOrEmpty,
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
                            'mxsApp/SET_SNACK_BAR_MESSAGE',
                            {
                                text: [this.vue.$mxs_t('info.disconnSuccessfully')],
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
        async reconnect({ state, commit, dispatch, getters }) {
            const active_sql_conn = state.active_sql_conn
            try {
                let connIds = []
                const wkeConnId = this.vue.$typy(getters.getCurrWkeConn, 'id').safeString
                const activeConnId = this.vue.$typy(active_sql_conn, 'id').safeString
                if (wkeConnId) connIds.push(wkeConnId)
                if (activeConnId) connIds.push(activeConnId)

                const allRes = await Promise.all(
                    connIds.map(id => {
                        return this.vue.$queryHttp.post(`/sql/${id}/reconnect`)
                    })
                )

                if (allRes.length && allRes.every(promise => promise.status === 204)) {
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.vue.$mxs_t('info.reconnSuccessfully')],
                            type: 'success',
                        },
                        { root: true }
                    )
                    await dispatch('schemaSidebar/initialFetch', {}, { root: true })
                } else
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.vue.$mxs_t('errors.reconnFailed')],
                            type: 'error',
                        },
                        { root: true }
                    )
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
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
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
