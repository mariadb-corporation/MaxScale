/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { genSetMutations, addDaysToNow } from '@/utils/helpers'
import { CMPL_SNIPPET_KIND } from '@/constants/workspace'
import { MXS_OBJ_TYPE_MAP } from '@/constants'

const states = () => ({
  sidebar_pct_width: 0,
  is_sidebar_collapsed: false,
  query_pane_pct_height: 60,
  is_fullscreen: false,
  query_row_limit: 10000,
  query_confirm_flag: true,
  query_history_expired_time: addDaysToNow(30), // Unix time
  query_show_sys_schemas_flag: true,
  tab_moves_focus: false,
  max_statements: 1000,
  identifier_auto_completion: true,
  def_conn_obj_type: MXS_OBJ_TYPE_MAP.LISTENERS,
  interactive_timeout: 28800,
  wait_timeout: 28800,
  query_history: [],
  query_snippets: [],
  del_all_conns_before_leave: true,
  show_confirm_dlg_before_leave: true,
})

// Place here any workspace states need to be persisted without being cleared when logging out
export default {
  namespaced: true,
  state: states(),
  mutations: {
    UPDATE_QUERY_HISTORY(state, { idx, payload }) {
      if (idx) state.query_history.splice(idx, 1)
      else state.query_history.unshift(payload)
    },
    UPDATE_QUERY_SNIPPETS(state, { idx, payload }) {
      if (idx) state.query_snippets.splice(idx, 1)
      else state.query_snippets.unshift(payload)
    },
    ...genSetMutations(states()),
  },
  getters: {
    snippetCompletionItems: (state) =>
      state.query_snippets.map((q) => ({
        label: q.name,
        detail: `SNIPPET - ${q.sql}`,
        insertText: q.sql,
        type: CMPL_SNIPPET_KIND,
      })),
  },
}
