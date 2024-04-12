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
import { t as typy } from 'typy'
import { queryEngines, queryCharsetCollationMap, queryDefDbCharsetMap } from '@/store/queryHelper'

const states = () => ({
  charset_collation_map: {},
  def_db_charset_map: {},
  engines: [],
})

export default {
  namespaced: true,
  state: states(),
  mutations: genSetMutations(states()),
  actions: {
    /**TODO: Convert this to a composable hook once the actions
     * in orm modules have been converted to composable hooks
     * @param {string} param.connId - connection id
     * @param {object} param.config - axios config
     */
    async queryDdlEditorSuppData({ state, commit }, param) {
      if (param.connId && param.config) {
        if (typy(state.engines).isEmptyArray) commit('SET_ENGINES', await queryEngines(param))
        if (typy(state.charset_collation_map).isEmptyObject)
          commit('SET_CHARSET_COLLATION_MAP', await queryCharsetCollationMap(param))
        if (typy(state.def_db_charset_map).isEmptyObject)
          commit('SET_DEF_DB_CHARSET_MAP', await queryDefDbCharsetMap(param))
      }
    },
  },
}
