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
import Worksheet from '@wsModels/Worksheet'
import QueryConn from '@wsModels/QueryConn'
import { QUERY_CONN_BINDING_TYPES } from '@/constants/workspace'

export default {
  namespaced: true,
  actions: {
    //TODO: Move this to its associated component and get rid of this state module
    /**
     * Check if there is a QueryEditor connected to the provided conn_name and set it as the
     * active worksheet. Otherwise, find an blank worksheet, set it as active show connection
     * dialog with pre-select object
     * @param {String} param.conn_name - connection name
     */
    async chooseQueryEditorWke({ commit }, { type, conn_name }) {
      const { $typy } = this.vue
      const queryEditorConns = QueryConn.getters('queryEditorConns')
      // Find connection
      const queryEditorConn = queryEditorConns.find(
        (c) => $typy(c, 'meta.name').safeString === conn_name
      )
      /**
       * If it is already bound to a QueryEditor, use the QueryEditor id for
       * setting active worksheet because the QueryEditor id is also Worksheet id.
       */
      if ($typy(queryEditorConn, 'query_editor_id').safeString)
        Worksheet.commit((state) => (state.active_wke_id = queryEditorConn.query_editor_id))
      else {
        const blankQueryEditorWke = Worksheet.query()
          .where(
            (w) =>
              $typy(w, 'etl_task_id').isNull &&
              $typy(w, 'query_editor_id').isNull &&
              $typy(w, 'erd_task_id').isNull
          )
          .first()
        // Use a blank query editor wke if there is one, otherwise create a new one
        if (blankQueryEditorWke)
          Worksheet.commit((state) => (state.active_wke_id = blankQueryEditorWke.id))
        else Worksheet.dispatch('insertQueryEditorWke')
        commit('queryConnsMem/SET_PRE_SELECT_CONN_RSRC', { type, id: conn_name }, { root: true })
        commit(
          'mxsWorkspace/SET_CONN_DLG',
          {
            is_opened: true,
            type: QUERY_CONN_BINDING_TYPES.QUERY_EDITOR,
          },
          { root: true }
        )
      }
    },
  },
}
