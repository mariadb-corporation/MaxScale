/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ErdTask from '@wsModels/ErdTask'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import Worksheet from '@wsModels/Worksheet'
import { CONN_TYPE_MAP } from '@/constants/workspace'

export default {
  namespaced: true,
  getters: {
    activeQueryEditorConn: () =>
      QueryConn.query().where('query_editor_id', QueryEditor.getters('activeId')).first() || {},
    activeQueryTabConn: () => {
      const activeQueryTabId = QueryEditor.getters('activeQueryTabId')
      if (!activeQueryTabId) return {}
      return QueryConn.query().where('query_tab_id', activeQueryTabId).first() || {}
    },
    activeSchema: (state, getters) => getters.activeQueryTabConn.active_db || '',
    activeEtlConns: () =>
      QueryConn.query().where('etl_task_id', Worksheet.getters('activeRecord').etl_task_id).get(),
    activeEtlSrcConn: (state, getters) =>
      getters.activeEtlConns.find((c) => c.binding_type === CONN_TYPE_MAP.ETL_SRC) || {},
    activeErdConn: () =>
      QueryConn.query().where('erd_task_id', ErdTask.getters('activeRecordId')).first() || {},
  },
}
