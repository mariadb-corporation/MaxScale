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
import allMemStatesModules from './allMemStatesModules'
import init, { defWorksheetState } from './initQueryEditorState'

export default {
    namespaced: true,
    state: {
        // Toolbar states
        is_fullscreen: false,
        // worksheet states
        worksheets_arr: init.get_def_worksheets_arr, // persisted
        active_wke_id: '',
    },
    mutations: {
        //Toolbar mutations
        SET_FULLSCREEN(state, payload) {
            state.is_fullscreen = payload
        },
        // worksheet mutations
        ADD_NEW_WKE(state) {
            state.worksheets_arr.push(defWorksheetState())
        },
        DELETE_WKE(state, id) {
            state.worksheets_arr = state.worksheets_arr.filter(wke => wke.id !== id)
        },
        UPDATE_WKE(state, { idx, wke }) {
            state.worksheets_arr = this.vue.$help.immutableUpdate(state.worksheets_arr, {
                [idx]: { $set: wke },
            })
        },
        SET_ACTIVE_WKE_ID(state, payload) {
            state.active_wke_id = payload
        },
        /**
         * This mutation resets all properties of the provided targetWke object to its initial states
         * except states that stores editor data
         * @param {Object} targetWke - wke to be reset
         */
        REFRESH_WKE(state, targetWke) {
            const idx = state.worksheets_arr.indexOf(targetWke)
            const wke = {
                ...targetWke,
                ...queryHelper.syncStateCreator('schemaSidebar'),
                name: 'WORKSHEET',
            }
            state.worksheets_arr = this.vue.$help.immutableUpdate(state.worksheets_arr, {
                [idx]: { $set: wke },
            })
        },
    },
    actions: {
        async chooseActiveWke({ state, commit, dispatch, rootState }) {
            const { type = 'blank_wke', id: paramId } = this.router.app.$route.params
            if (paramId) {
                if (type !== 'blank_wke') {
                    /**
                     * Check if there is a worksheet connected to the provided resource id (paramId)
                     * then if it's not the current active worksheet, change current worksheet tab to targetWke.
                     * Otherwise, find an empty worksheet(has not been bound to a connection), set it as active and
                     * dispatch SET_PRE_SELECT_CONN_RSRC to open connection dialog
                     */
                    const targetSession = rootState.querySession.query_sessions.find(
                        s => this.vue.$typy(s, 'active_sql_conn.name').safeString === paramId
                    )
                    const targetWke = state.worksheets_arr.find(
                        w => w.id === this.vue.$typy(targetSession, 'wke_id_fk').safeString
                    )
                    if (targetWke) {
                        if (state.active_wke_id !== targetWke.id)
                            commit('SET_ACTIVE_WKE_ID', targetWke.id)
                        commit(
                            'querySession/SET_ACTIVE_SESSION_BY_WKE_ID_MAP',
                            {
                                id: targetWke.id,
                                payload:
                                    rootState.querySession.active_session_by_wke_id_map[
                                        targetWke.id
                                    ],
                            },
                            { root: true }
                        )
                    } else {
                        const blankSession = rootState.querySession.query_sessions.find(
                            s => this.vue.$typy(s, 'active_sql_conn').isEmptyObject
                        )
                        // Use a blank wke if there is one, otherwise create a new one
                        const blankWke = state.worksheets_arr.find(
                            wke => wke.id === this.vue.$typy(blankSession, 'wke_id_fk').safeString
                        )
                        if (blankWke) {
                            commit('SET_ACTIVE_WKE_ID', blankWke.id)
                            commit(
                                'querySession/SET_ACTIVE_SESSION_BY_WKE_ID_MAP',
                                {
                                    id: blankWke.id,
                                    payload: blankSession.id,
                                },
                                { root: true }
                            )
                        } else await dispatch('addNewWs')
                        commit(
                            'queryConn/SET_PRE_SELECT_CONN_RSRC',
                            { type, id: paramId },
                            { root: true }
                        )
                    }
                }
            } else if (state.worksheets_arr.length) {
                // set the first wke as active if route param id is not specified
                const activeWkeId = state.worksheets_arr[0].id
                commit('SET_ACTIVE_WKE_ID', activeWkeId)
                dispatch('updateRoute', activeWkeId)
            }
        },
        /**
         * This handles updating route for the current active worksheet.
         * If it is bound to a connection, it navigates route to the nested route. i.e /query/:resourceType/:resourceId
         * Otherwise, it uses worksheet id as nested route id. i.e. /query/blank_wke/:wkeId.
         * This function must be called in the following cases:
         * 1. When $route changes. e.g. The use edits url or enter page with an absolute link
         * 2. When active_wke_id is changed. e.g. The user creates new worksheet or navigate between worksheets
         * 3. When active_sql_conn is changed. e.g. The user selects connection in the dropdown or opens new one
         * 4. When the connection is unlinked from the worksheet
         * @param {String} wkeId - worksheet id
         */
        updateRoute({ state, rootState }, wkeId) {
            let from = this.router.app.$route.path,
                to = `/query/blank_wke/${wkeId}`
            const targetWke = state.worksheets_arr.find(w => w.id === wkeId)
            const sessionId = rootState.querySession.active_session_by_wke_id_map[targetWke.id]
            const session = rootState.querySession.query_sessions.find(s => s.id === sessionId)
            const { type, name } = this.vue.$typy(session, 'active_sql_conn').safeObjectOrEmpty
            if (name) to = `/query/${type}/${name}`
            if (from !== to) this.router.push(to)
        },
        async addNewWs({ commit, state, dispatch }) {
            try {
                commit('ADD_NEW_WKE')
                const new_active_wke_id = state.worksheets_arr[state.worksheets_arr.length - 1].id
                commit('SET_ACTIVE_WKE_ID', new_active_wke_id)
                await dispatch(
                    'querySession/handleAddNewSession',
                    { wke_id: new_active_wke_id },
                    { root: true }
                )
            } catch (e) {
                this.vue.$logger('store-wke-addNewWs').error(e)
                commit(
                    'SET_SNACK_BAR_MESSAGE',
                    {
                        text: [this.i18n.t('errors.persistentStorage')],
                        type: 'error',
                    },
                    { root: true }
                )
            }
        },
        async handleDeleteWke({ commit, dispatch, state, getters, rootGetters }, id) {
            try {
                const currActiveWkeId = state.active_wke_id
                // release module memory states
                dispatch('releaseQueryModulesMem', id)
                await dispatch('handleDeleteWkeSessions', id)
                commit('DELETE_WKE', id)
                /**
                 * Handle a case where the current active worksheet is not the one being deleted.
                 * In that case, the worksheet and its sessions are not synced with syncedStates and memStates
                 * So it should dispatch handleSyncWke and querySession/handleSyncSession
                 * to get the right data
                 */
                if (currActiveWkeId !== id) {
                    // get the next active wke, it's chosen automatically by v-tabs .i.e state.active_wke_id
                    const activeWke = getters.getActiveWke
                    if (activeWke) {
                        dispatch('handleSyncWke', activeWke)
                        dispatch(
                            'querySession/handleSyncSession',
                            rootGetters['querySession/getActiveSession'],
                            { root: true }
                        )
                    }
                }
            } catch (e) {
                this.vue.$logger('store-wke-handleDeleteWke').error(e)
            }
        },
        async handleDeleteWkeSessions({ dispatch, commit, rootState, rootGetters }, wke_id) {
            try {
                const sessions = rootGetters['querySession/getSessionsByWkeId'](wke_id)
                const { SESSION } = rootState.app_config.QUERY_CONN_BINDING_TYPES
                // Disconnect all cloned sessions
                for (const session of sessions) {
                    if (session.wke_id_fk === wke_id) {
                        const {
                            active_sql_conn: { id: conn_id = null, binding_type = '' } = {},
                        } = session
                        if (conn_id && binding_type === SESSION)
                            await dispatch(
                                'queryConn/disconnectClone',
                                { id: conn_id },
                                { root: true }
                            )
                    }
                }
                // finally release mem and delete session objects
                sessions.forEach(s => {
                    dispatch(
                        'querySession/resetSessionStates',
                        { session_id: s.id },
                        { root: true }
                    )
                    commit('querySession/DELETE_SESSION', s.id, { root: true })
                })
                commit(
                    'querySession/SET_ACTIVE_SESSION_BY_WKE_ID_MAP',
                    { id: wke_id },
                    { root: true }
                )
            } catch (e) {
                this.vue.$logger('store-wke-handleDeleteWkeSessions').error(e)
            }
        },
        /**
         * @param {Object} param.wke - worksheet object to be sync to flat states
         */
        handleSyncWke({ commit }, wke) {
            commit('schemaSidebar/SYNC_WITH_PERSISTED_OBJ', wke, { root: true })
        },
        /**
         * Release memory for target wke when delete a worksheet or disconnect a
         * connection from a worksheet
         * @param {String} param.wke_id - worksheet id.
         */
        releaseQueryModulesMem({ commit }, wke_id) {
            //release memory only for schemaSidebar here.
            Object.keys(allMemStatesModules).forEach(namespace => {
                if (namespace === 'schemaSidebar')
                    queryHelper.releaseMemory({
                        namespace,
                        commit,
                        id: wke_id,
                        memStates: allMemStatesModules[namespace],
                    })
            })
        },
        /**
         * wke cleanup
         * release memStates that uses wke id as key,
         * refresh wke state to its initial state.
         * Call this function when the disconnect the connection in `connection-manager` component
         * @param {String} wkeBoundCnnId - connection id that is bound to the first session tab
         */
        resetWkeStates({ commit, rootState, dispatch, rootGetters, getters }, wkeBoundCnnId) {
            const defSession = rootGetters['querySession/getSessionByConnId'](wkeBoundCnnId)
            const targetWke = getters.getWkeBySession(defSession)
            if (targetWke) {
                dispatch('releaseQueryModulesMem', targetWke.id)
                commit('REFRESH_WKE', targetWke)
                const freshWke = rootState.wke.worksheets_arr.find(wke => wke.id === targetWke.id)
                dispatch('handleSyncWke', freshWke)
            }
        },
    },
    getters: {
        getActiveWke: state => {
            return state.worksheets_arr.find(wke => wke.id === state.active_wke_id) || {}
        },
        getWkeBySession: state => {
            return session => state.worksheets_arr.find(w => w.id === session.wke_id_fk) || {}
        },
    },
}
