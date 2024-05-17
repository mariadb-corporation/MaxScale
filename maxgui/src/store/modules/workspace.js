/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { genSetMutations } from '@/utils/helpers'
import { QUERY_CONN_BINDING_TYPES, ETL_DEF_POLLING_INTERVAL } from '@/constants/workspace'

const states = () => ({
  hidden_comp: [''],
  migr_dlg: { is_opened: false, etl_task_id: '', type: '' },
  gen_erd_dlg: {
    is_opened: false,
    preselected_schemas: [],
    connection: null,
    gen_in_new_ws: false, // generate erd in a new worksheet
  },
  exec_sql_dlg: {
    is_opened: false,
    editor_height: 250,
    sql: '',
    extra_info: '',
    /**
     * @property {object} data - Contains res.data.data.attributes of a query
     * @property {object} error
     */
    result: null,
    on_exec: () => null,
    after_cancel: () => null,
  },
  confirm_dlg: {
    is_opened: false,
    title: '',
    i18n_interpolation: { keypath: '', values: [] },
    save_text: 'save',
    cancel_text: 'dontSave',
    on_save: () => null,
    after_cancel: () => null,
  },
  etl_polling_interval: ETL_DEF_POLLING_INTERVAL,
  //Below states needed for the workspace package so it can be used in SkySQL
  conn_dlg: { is_opened: false, type: QUERY_CONN_BINDING_TYPES.QUERY_EDITOR },
})

export default {
  namespaced: true,
  state: states(),
  mutations: genSetMutations(states()),
  getters: {
    execSqlDlgResult: (state) => state.exec_sql_dlg.result,
    getExecErr: (state, getters) => {
      const { error } = getters.execSqlDlgResult || {}
      return error
    },
    isExecFailed: (state, getters) => {
      if (getters.execSqlDlgResult) return Boolean(getters.getExecErr)
      return false
    },
  },
}
