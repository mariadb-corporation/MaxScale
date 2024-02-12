/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { genSetMutations } from '@/utils/helpers'

const states = () => ({
  overlay_type: '',
  gbl_tooltip_data: null,
  is_session_alive: true,
})

export default {
  namespaced: true,
  state: { ...states(), snackbar_message: { status: false, text: '', type: 'info' } },
  mutations: {
    ...genSetMutations(states()),
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
  },
}
