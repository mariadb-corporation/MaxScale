/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import queryHelper from './queryHelper'
import allMemStatesModules from './allMemStatesModules'
import init, { defSessionState } from './initQueryEditorState'

export default {
    namespaced: true,
    state: {
        query_sessions: init.get_def_query_sessions, // persisted
        // each key holds a string value of the active session id of a worksheet
        active_session_by_wke_id_map: {}, // persisted
    },
    mutations: {
        ADD_NEW_SESSION(state, { wke_id, name: custName }) {
            let name = 'Query Tab 1',
                count = 1
            const sessions_in_wke = state.query_sessions.filter(s => s.wke_id_fk === wke_id)
            if (sessions_in_wke.length) {
                const lastSession = sessions_in_wke[sessions_in_wke.length - 1]
                count = lastSession.count + 1
                name = `Query Tab ${count}`
            }
            const newSession = { ...defSessionState(wke_id), name: custName || name, count }
            state.query_sessions.push(newSession)
        },
        DELETE_SESSION(state, id) {
            state.query_sessions = state.query_sessions.filter(s => s.id !== id)
        },
        /**
         * UPDATE_SESSION must be called before any mutations that mutates a property of a session.
         * e.g. SET_BLOB_FILE, SET_QUERY_TXT
         */
        UPDATE_SESSION(state, session) {
            const idx = state.query_sessions.findIndex(s => s.id === session.id)
            state.query_sessions = this.vue.$helpers.immutableUpdate(state.query_sessions, {
                [idx]: { $set: session },
            })
        },
        SET_ACTIVE_SESSION_BY_WKE_ID_MAP(state, { id, payload }) {
            if (!payload) this.vue.$delete(state.active_session_by_wke_id_map, id)
            else
                state.active_session_by_wke_id_map = {
                    ...state.active_session_by_wke_id_map,
                    [id]: payload,
                }
        },
        /**
         * This mutation resets all properties of the provided targetSession object
         * to its initial states except some properties
         * @param {Object} session - session to be reset
         */
        REFRESH_SESSION_OF_A_WKE(state, session) {
            const idx = state.query_sessions.findIndex(s => s.id === session.id)
            let s = { ...this.vue.$helpers.lodash.cloneDeep(session) }
            // Reset the name except the session having blob_file
            if (this.vue.$typy(s, 'blob_file').isEmptyObject) s.name = `Query Tab ${s.count}`
            // Keys that won't have its value refreshed
            const reservedKeys = ['id', 'name', 'count', 'blob_file', 'query_txt']
            state.query_sessions = this.vue.$helpers.immutableUpdate(state.query_sessions, {
                [idx]: {
                    $set: {
                        ...s,
                        ...this.vue.$helpers.lodash.pickBy(
                            defSessionState(s.wke_id_fk),
                            (v, key) => !reservedKeys.includes(key)
                        ),
                    },
                },
            })
        },
    },
    actions: {
        /**
         * This action add new session to the provided worksheet id.
         * It uses the worksheet connection to clone into a new connection and bind it
         * to the session being created.
         * @param {String} param.wke_id - worksheet id
         * @param {String} param.name - session name. If not provided, it'll be auto generated
         */
        async handleAddNewSession(
            { commit, state, dispatch, rootState, rootGetters },
            { wke_id, name }
        ) {
            try {
                // add a blank session
                commit('ADD_NEW_SESSION', { wke_id, name })
                const newSessionId = state.query_sessions[state.query_sessions.length - 1].id
                // Clone the wke conn and bind it to the new session
                const wke_conn = rootGetters['queryConn/getWkeConnByWkeId'](wke_id)
                let sessionConn
                if (wke_conn.id) {
                    await dispatch(
                        'queryConn/cloneConn',
                        {
                            conn_to_be_cloned: wke_conn,
                            binding_type:
                                rootState.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES.SESSION,
                            session_id_fk: newSessionId,
                            getCloneObjRes: obj => (sessionConn = obj),
                        },
                        { root: true }
                    )
                }
                // Change active session
                commit('SET_ACTIVE_SESSION_BY_WKE_ID_MAP', {
                    id: wke_id,
                    payload: newSessionId,
                })
                // Switch to use new clone connection
                if (sessionConn)
                    commit(
                        'queryConn/SET_ACTIVE_SQL_CONN',
                        {
                            payload: sessionConn,
                            id: newSessionId,
                        },
                        { root: true }
                    )
            } catch (e) {
                this.vue.$logger('store-querySession-handleAddNewSession').error(e)
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    { text: [this.vue.$mxs_t('errors.persistentStorage')], type: 'error' },
                    { root: true }
                )
            }
        },
        /**
         * This handle deletes all connections bound to this session and release memory
         * before deleting the session. A session object tab technically can be bound to a connection,
         * but when the user switches to a new worksheet connection from an existing worksheet
         * connection in the <conn-man-ctr/> component, there are now 2 connections to different
         * servers with the same session_id_fk. So it must be deleted as well.
         * @param {Object} session - A session object

         */
        async handleDeleteSession({ commit, rootState, dispatch }, session) {
            const bound_conns = Object.values(rootState.queryConn.sql_conns).filter(
                c => c.session_id_fk === session.id
            )
            for (const { id } of bound_conns) {
                if (id) await dispatch('queryConn/disconnectClone', { id }, { root: true })
            }
            dispatch('releaseQueryModulesMem', session.id)
            commit('DELETE_SESSION', session.id)
        },
        /**
         * Clear all session's data except active_sql_conn data
         * @param {Object} param.session - session to be cleared
         */
        handleClearTheLastSession({ commit, dispatch, getters }, session) {
            const freshSession = {
                ...defSessionState(session.wke_id_fk),
                id: session.id,
                name: 'Query Tab 1',
                count: 1,
                active_sql_conn: session.active_sql_conn,
            }
            commit('UPDATE_SESSION', freshSession)
            dispatch('releaseQueryModulesMem', session.id)
            // only sync if targetSession is the active session of the worksheet
            if (getters.getActiveSessionId === session.id)
                dispatch('handleSyncSession', freshSession)
        },
        /**
         * @param {Object} param.session - session object to be sync to flat states
         */
        handleSyncSession({ commit }, session) {
            commit('queryConn/SYNC_WITH_PERSISTED_OBJ', session, { root: true })
            commit('queryResult/SYNC_WITH_PERSISTED_OBJ', session, { root: true })
            commit('editor/SYNC_WITH_PERSISTED_OBJ', session, { root: true })
        },
        /**
         * Release memory for target wke when delete a session
         * @param {String} param.session_id - session id.
         */
        releaseQueryModulesMem({ commit }, session_id) {
            Object.keys(allMemStatesModules).forEach(namespace => {
                // Only 'editor', 'queryConn', 'queryResult' modules have memStates keyed by session_id
                if (namespace !== 'schemaSidebar')
                    queryHelper.releaseMemory({
                        namespace,
                        commit,
                        id: session_id,
                        memStates: allMemStatesModules[namespace],
                    })
            })
        },
        /**
         * sessions cleanup
         * release memStates that uses session id as key,
         * refresh targetSession to its initial state. The target session
         * can be either found by using conn_id or session_id
         * @param {String} param.conn_id - connection id
         * @param {String} param.session_id - session id
         */
        resetSessionStates(
            { state, rootState, commit, dispatch, getters },
            { conn_id, session_id }
        ) {
            const targetSession = conn_id
                ? getters.getSessionByConnId(conn_id)
                : state.query_sessions.find(s => s.id === session_id)
            dispatch('releaseQueryModulesMem', targetSession.id)
            commit('REFRESH_SESSION_OF_A_WKE', targetSession)
            const freshSession = rootState.querySession.query_sessions.find(
                s => s.id === targetSession.id
            )
            // only sync if targetSession is the active session of the worksheet
            if (getters.getActiveSessionId === session_id)
                dispatch('handleSyncSession', freshSession)
        },
    },
    getters: {
        getActiveSessionId: (state, getters, rootState) => {
            return state.active_session_by_wke_id_map[rootState.wke.active_wke_id]
        },
        getActiveSession: (state, getters) => {
            return state.query_sessions.find(s => s.id === getters.getActiveSessionId) || {}
        },
        getSessionsOfActiveWke: (state, getters, rootState) => {
            return state.query_sessions.filter(s => s.wke_id_fk === rootState.wke.active_wke_id)
        },
        getSessionsByWkeId: state => {
            return wke_id => state.query_sessions.filter(s => s.wke_id_fk === wke_id)
        },
        getSessionByConnId: state => {
            return conn_id =>
                state.query_sessions.find(
                    s => s.active_sql_conn && s.active_sql_conn.id === conn_id
                ) || {}
        },
        getSessionById: state => id => state.query_sessions.find(s => s.id === id) || {},
    },
}
