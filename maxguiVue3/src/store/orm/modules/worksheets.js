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
import QueryConn from '@wsModels/QueryConn'
import QueryTab from '@wsModels/QueryTab'
import Worksheet from '@wsModels/Worksheet'
import WorksheetTmp from '@wsModels/WorksheetTmp'
import { t as typy } from 'typy'

export default {
  namespaced: true,
  state: { active_wke_id: null }, // Persistence state
  getters: {
    activeId: (state) => state.active_wke_id,
    activeRecord: (state) => Worksheet.find(state.active_wke_id) || {},
    activeRequestConfig: (state, getters) => getters.findRequestConfig(state.active_wke_id),
    // Method-style getters (Uncached getters)
    findEtlTaskWkeId: () => (etl_task_id) =>
      typy(Worksheet.query().where('etl_task_id', etl_task_id).first(), 'id').safeString,
    findErdTaskWkeId: () => (erd_task_id) =>
      typy(Worksheet.query().where('erd_task_id', erd_task_id).first(), 'id').safeString,
    findRequestConfig: () => (wkeId) =>
      typy(WorksheetTmp.find(wkeId), 'request_config').safeObjectOrEmpty,
    findEtlTaskRequestConfig: (state, getters) => (id) =>
      getters.findRequestConfig(getters.findEtlTaskWkeId(id)),
    findConnRequestConfig: (state, getters) => (id) => {
      const { etl_task_id, query_tab_id, query_editor_id, erd_task_id } = typy(
        QueryConn.find(id)
      ).safeObjectOrEmpty

      if (etl_task_id) return getters.findRequestConfig(getters.findEtlTaskWkeId(etl_task_id))
      else if (erd_task_id) return getters.findRequestConfig(getters.findErdTaskWkeId(erd_task_id))
      else if (query_editor_id) return getters.findRequestConfig(query_editor_id)
      else if (query_tab_id)
        return getters.findRequestConfig(
          typy(QueryTab.find(query_tab_id), 'query_editor_id').safeString
        )
      return {}
    },
  },
}
