/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import TxtEditor from '@wsModels/TxtEditor'
import { supported } from 'browser-fs-access'
import localForage from 'localforage'
import { genSetMutations } from '@share/utils/helpers'
import { FILE_SYS_ACCESS_NAMESPACE } from '@wsSrc/constants'

const states = () => ({
    /**
     * Key: query_tab_id or editor id as they are the same
     * Value is defined as below
     * @property {object} file_handle - <FileSystemFileHandle>
     * @property {string} txt - file text
     */
    file_handle_data_map: {},
})

/*
 * vuex-orm and vuex-persist serialize data as a JSON string but FileSystemFileHandle
 * can't be serialized with JSON.stringify. So this module is created as a workaround
 * to manually persist file handle to IndexedDB via localForage. Currently only
 * IndexedDB can serialize file handle.
 * https://github.com/GoogleChromeLabs/browser-fs-access/issues/30
 */
export default {
    namespaced: true,
    state: states(),
    mutations: {
        UPDATE_FILE_HANDLE_DATA_MAP(state, payload) {
            state.file_handle_data_map[payload.id] = {
                ...(state.file_handle_data_map[payload.id] || {}),
                ...payload.data,
            }
        },
        DELETE_FILE_HANDLE_DATA(state, id) {
            this.vue.$delete(state.file_handle_data_map, id)
        },
        ...genSetMutations(states()),
    },
    actions: {
        async initStorage({ commit, state }) {
            const storage = await localForage.getItem(FILE_SYS_ACCESS_NAMESPACE)
            commit('SET_FILE_HANDLE_DATA_MAP', storage || {})
            await localForage.setItem(FILE_SYS_ACCESS_NAMESPACE, state.file_handle_data_map)
        },
        async updateFileHandleDataMap({ commit, state }, payload) {
            commit('UPDATE_FILE_HANDLE_DATA_MAP', payload)
            // Workaround, update editor query_txt so getIsQueryTabUnsaved getter can recompute
            TxtEditor.update({ where: payload.id, data: { query_txt: payload.data.txt } })
            await localForage.setItem(FILE_SYS_ACCESS_NAMESPACE, state.file_handle_data_map)
        },
        async deleteFileHandleData({ commit, state }, id) {
            commit('DELETE_FILE_HANDLE_DATA', id)
            await localForage.setItem(FILE_SYS_ACCESS_NAMESPACE, state.file_handle_data_map)
        },
    },
    getters: {
        //browser fs getters
        hasFileSystemReadOnlyAccess: () => Boolean(supported),
        hasFileSystemRWAccess: (state, getters) =>
            getters.hasFileSystemReadOnlyAccess && window.location.protocol.includes('https'),
        getFileHandleData: state => id => state.file_handle_data_map[id] || {},
        /**
         * @returns {<FileSystemFileHandle>} fileHandle
         */
        getFileHandle: (state, getters) => id => getters.getFileHandleData(id).file_handle || {},
        /**
         * @returns {String} FileSystemFileHandle name
         */
        getFileHandleName: (state, getters) => id => getters.getFileHandle(id).name || '',
        getIsFileHandleValid: (state, getters) => id => Boolean(getters.getFileHandleName(id)),
        getIsQueryTabUnsaved: (state, getters) => id => {
            const { query_txt = '' } = TxtEditor.find(id) || {}

            const {
                txt: file_handle_txt = '',
                file_handle: { name: file_handle_name = '' } = {},
            } = getters.getFileHandleData(id)

            // no unsaved changes if it's a blank queryTab
            if (!query_txt && !file_handle_name) return false
            // If there is no file opened but there is value for query_txt
            // If there is a file opened and query_txt is !== its original file text, return true
            return file_handle_txt !== query_txt
        },
    },
}
