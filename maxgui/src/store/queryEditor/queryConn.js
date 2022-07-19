/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-04-08
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
                const res = await this.$http.get(`/${resourceType}?fields[${resourceType}]=id`)
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
            { state, commit, dispatch, rootGetters },
            { silentValidation = false } = {}
        ) {
            if (!silentValidation) commit('SET_IS_VALIDATING_CONN', true)
            try {
                const active_session_id = rootGetters['querySession/getActiveSessionId']
                const res = await this.$http.get(`/sql/`)
                const resConnMap = this.vue.$help.lodash.keyBy(res.data.data, 'id')
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
                    //deleteInvalidConn
                    invalidCnctIds.forEach(id => {
                        this.vue.$help.deleteCookie(`conn_id_body_${id}`)
                        dispatch('querySession/resetSessionStates', { conn_id: id }, { root: true })
                    })

                    commit('SET_SQL_CONNS', validSqlConns)
                    // update active_sql_conn attributes
                    if (state.active_sql_conn.id) {
                        const active_sql_conn = validSqlConns[state.active_sql_conn.id]
                        commit('SET_ACTIVE_SQL_CONN', {
                            payload: active_sql_conn,
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
         * @param {Object} param.body - request body
         * @param {String} param.resourceType - services, servers or listeners.
         */
        async openConnect({ dispatch, commit, rootState, rootGetters }, { body, resourceType }) {
            // activeWkeSessions length always >=1 as the default session will be always created on startup
            const activeWkeSessions = rootGetters['querySession/getSessionsOfActiveWke']
            const active_session_id = rootGetters['querySession/getActiveSessionId']
            try {
                // create the connection
                const res = await this.$http.post(`/sql?persist=yes&max-age=86400`, body)
                if (res.status === 201) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
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
                        binding_type: rootState.app_config.QUERY_CONN_BINDING_TYPES.SESSION,
                    }
                    commit('ADD_SQL_CONN', sql_conn)
                    // sync the first session tab to persisted obj
                    const defSession = activeWkeSessions[0]
                    queryHelper.syncToPersistedObj({
                        scope: this,
                        data: { active_sql_conn: sql_conn },
                        id: defSession.id,
                        persistedArrayPath: 'querySession.query_sessions',
                    })
                    // check if there are other session tabs, clone it to other session tabs
                    const otherSessions = activeWkeSessions.filter(s => s.id !== defSession.id)
                    if (otherSessions.length) {
                        // clone the connection and bind it to all other session tabs
                        for (const s of otherSessions) {
                            let cloneConnObj
                            await dispatch('cloneConn', {
                                conn_to_be_cloned: sql_conn,
                                binding_type: rootState.app_config.QUERY_CONN_BINDING_TYPES.SESSION,
                                getCloneObjRes: obj => (cloneConnObj = obj),
                            })
                            // sync it
                            if (s.id !== active_session_id)
                                queryHelper.syncToPersistedObj({
                                    scope: this,
                                    data: { active_sql_conn: cloneConnObj },
                                    id: s.id,
                                    persistedArrayPath: 'querySession.query_sessions',
                                })
                            // bind the connection to the active session tab, this will also sync it
                            else {
                                commit('SET_ACTIVE_SQL_CONN', { payload: cloneConnObj, id: s.id })
                                commit('PATCH_IS_CONN_BUSY_MAP', {
                                    id: active_session_id,
                                    payload: { value: false },
                                })
                            }
                        }
                    }
                    // set active conn once sessions are bound to cloned connections to avoid concurrent query
                    if (active_session_id === defSession.id)
                        commit('SET_ACTIVE_SQL_CONN', { payload: sql_conn, id: defSession.id })
                    if (body.db) await dispatch('schemaSidebar/useDb', body.db, { root: true })
                    commit('SET_CONN_ERR_STATE', false)
                }
            } catch (e) {
                this.vue.$logger('store-queryConn-openConnect').error(e)
                commit('SET_CONN_ERR_STATE', true)
            }
        },
        /**
         *  Clone a connection
         * @param {Object} param.conn_to_be_cloned - connection to be cloned
         * @param {String} param.binding_type - binding_type. Check QUERY_CONN_BINDING_TYPES
         * @param {Function} param.getCloneObjRes - get the result of the clone object
         */
        async cloneConn({ commit }, { conn_to_be_cloned, binding_type, getCloneObjRes }) {
            try {
                const res = await this.$queryHttp.post(
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
                    }
                    if (this.vue.$help.isFunction(getCloneObjRes)) getCloneObjRes(conn)
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
                const res = await this.$http.delete(`/sql/${id}`)
                if (res.status === 204) commit('DELETE_SQL_CONN', state.sql_conns[id])
            } catch (e) {
                this.vue.$logger('store-queryConn-disconnectClone').error(e)
            }
        },
        /**
         * This handles deleting a connection. If the provided connection id is the first opened connection
         * of a worksheet, then all of its clone connections will be also deleted. Otherwise,
         * it will find the default connection using `clone_of_conn_id` attribute and delete them all.
         * This action is meant to be used by `connection-manager` component to "unlink" a resource connection
         * from the worksheet. It's also be used by the `disconnectAll` action to delete all connection when
         * leaving the page.
         * @param {Boolean} param.showSnackbar - should show success message or not
         * @param {Number} param.id - connection id that is bound to the first session tab
         */
        async disconnect(
            { state, commit, dispatch, rootState },
            { showSnackbar, id: targetConnId }
        ) {
            try {
                if (state.sql_conns[targetConnId]) {
                    const clonedConnIds = Object.values(state.sql_conns)
                        .filter(
                            c =>
                                c.clone_of_conn_id === targetConnId &&
                                c.binding_type ===
                                    rootState.app_config.QUERY_CONN_BINDING_TYPES.SESSION
                        )
                        .map(c => c.id)
                    const cnnIdsToBeDeleted = [targetConnId, ...clonedConnIds]
                    dispatch('wke/resetWkeStates', targetConnId, { root: true })

                    const allRes = await Promise.all(
                        cnnIdsToBeDeleted.map(id => {
                            commit('DELETE_SQL_CONN', state.sql_conns[id])
                            dispatch(
                                'querySession/resetSessionStates',
                                { conn_id: id },
                                { root: true }
                            )
                            return this.$http.delete(`/sql/${id}`)
                        })
                    )

                    if (allRes.every(promise => promise.status === 204) && showSnackbar)
                        commit(
                            'SET_SNACK_BAR_MESSAGE',
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
        async disconnectAll({ state, dispatch }) {
            try {
                for (const id of Object.keys(state.sql_conns))
                    await dispatch('disconnect', { showSnackbar: false, id })
            } catch (e) {
                this.vue.$logger('store-queryConn-disconnectAll').error(e)
            }
        },
        async reconnect({ state, commit, dispatch }) {
            const active_sql_conn = state.active_sql_conn
            try {
                const res = await this.$queryHttp.post(`/sql/${active_sql_conn.id}/reconnect`)
                if (res.status === 204) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
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
                        'SET_SNACK_BAR_MESSAGE',
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

        // Reset all when there is no active connections
        resetAllStates({ rootState, commit }) {
            for (const targetWke of rootState.wke.worksheets_arr) {
                commit('wke/REFRESH_WKE', targetWke, { root: true })
                commit('querySession/REFRESH_SESSIONS_OF_A_WKE', targetWke, { root: true })
            }
        },
    },
    getters: {
        // return the first found object, so it only works for BACKGROUND connection for now
        getCloneConn: state => {
            /**
             * @param {Number} - active connection id that was cloned
             */
            return ({ clone_of_conn_id, binding_type }) =>
                Object.values(state.sql_conns).find(
                    conn =>
                        conn.clone_of_conn_id === clone_of_conn_id &&
                        conn.binding_type === binding_type
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
        getWkeFirstSessConnByWkeId: (state, getters, rootState) => {
            const query_sessions = rootState.querySession.query_sessions
            return wke_id => {
                const def_session =
                    query_sessions.find(
                        s =>
                            s.wke_id_fk === wke_id &&
                            s.active_sql_conn &&
                            s.active_sql_conn.binding_type ===
                                rootState.app_config.QUERY_CONN_BINDING_TYPES.SESSION
                    ) || {}
                return def_session.active_sql_conn || {}
            }
        },
    },
}
