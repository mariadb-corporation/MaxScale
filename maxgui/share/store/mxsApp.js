/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    namespaced: true,
    state: {
        snackbar_message: { status: false, text: '', type: 'info' },
        overlay_type: false,
        truncate_tooltip_item: null,
        is_session_alive: true,
    },
    mutations: {
        SET_OVERLAY_TYPE(state, type) {
            state.overlay_type = type
        },
        /**
         * @param {Object} obj Object snackbar_message
         * @param {Array} obj.text An array of string
         * @param {String} obj.type Type of response
         */
        SET_SNACK_BAR_MESSAGE(state, obj) {
            const { text, type, status = true } = obj
            state.snackbar_message.status = status
            state.snackbar_message.text = text
            state.snackbar_message.type = type
        },
        SET_TRUNCATE_TOOLTIP_ITEM(state, obj) {
            state.truncate_tooltip_item = obj
        },
        SET_IS_SESSION_ALIVE(state, payload) {
            state.is_session_alive = payload
        },
    },
}
