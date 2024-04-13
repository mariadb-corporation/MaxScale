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
import EtlTask from '@wsModels/EtlTask'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import Worksheet from '@wsModels/Worksheet'
import { ETL_STATUS } from '@/constants/workspace'

export default {
  namespaced: true,
  getters: {
    activeRecord: () => EtlTask.find(Worksheet.getters('activeRecord').etl_task_id) || {},
    // Method-style getters (Uncached getters)
    findRecord: () => (id) => EtlTask.find(id) || {},
    isTaskCancelledById: (state, getters) => (id) =>
      getters.findRecord(id).status === ETL_STATUS.CANCELED,
    findPersistedRes: (state, getters) => (id) => getters.findRecord(id).res || {},
    findTmpRecord: () => (id) => EtlTaskTmp.find(id) || {},
    findEtlRes: (state, getters) => (id) => getters.findTmpRecord(id).etl_res,
    findSrcSchemaTree: (state, getters) => (id) => getters.findTmpRecord(id).src_schema_tree,
    findCreateMode: (state, getters) => (id) => getters.findTmpRecord(id).create_mode,
    findMigrationObjs: (state, getters) => (id) => getters.findTmpRecord(id).migration_objs,
    findResTables: (state, getters) => (id) => {
      const { tables = [] } = getters.findEtlRes(id) || getters.findPersistedRes(id)
      return tables
    },
    findResStage: (state, getters) => (id) => {
      const { stage = '' } = getters.findEtlRes(id) || getters.findPersistedRes(id)
      return stage
    },
  },
}
