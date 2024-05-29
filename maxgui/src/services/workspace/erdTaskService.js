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
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import queryConnService from '@wsServices/queryConnService'
import { t as typy } from 'typy'

async function cascadeDelete(payload) {
  const entityIds = ErdTask.filterEntity(ErdTask, payload).map((entity) => entity.id)
  for (const id of entityIds) {
    const { id: connId } = QueryConn.query().where('erd_task_id', id).first() || {}
    // delete the connection
    if (connId) await queryConnService.disconnect({ id: connId })
    ErdTask.delete(id) // delete itself
    // delete record in its the relational tables
    ErdTaskTmp.delete(id)
  }
}

/**
 * Init ErdTask entities if they don't exist in the active worksheet.
 * @param {object} param.erdTaskData - predefined data for ErdTask
 * @param {object} param.erdTaskTmpData - predefined data for ErdTaskTmp
 */
function initEntities({ erdTaskData = {}, erdTaskTmpData = {} } = {}) {
  const wkeId = Worksheet.getters('activeId')
  const lastErdTask = ErdTask.query().last()
  const count = typy(lastErdTask, 'count').safeNumber + 1
  const erdName = `ERD ${count}`
  if (!ErdTask.find(wkeId)) {
    // Insert an ErdTask and its mandatory relational entities
    ErdTask.insert({ data: { id: wkeId, count, ...erdTaskData } })
    ErdTaskTmp.insert({ data: { id: wkeId, ...erdTaskTmpData } })
  }
  Worksheet.update({ where: wkeId, data: { erd_task_id: wkeId, name: erdName } })
}

/**
 * @param {number} idx
 * @returns {Promise}
 */
async function updateActiveHistoryIdx(idx) {
  return await ErdTaskTmp.update({
    where: ErdTask.getters('activeRecordId'),
    data: { active_history_idx: idx },
  })
}

function setNodesHistory(newHistory) {
  ErdTaskTmp.update({
    where: ErdTask.getters('activeRecordId'),
    data: { nodes_history: newHistory },
  }).then(async () => await updateActiveHistoryIdx(newHistory.length - 1))
}

function updateNodesHistory(nodeMap) {
  const currentHistory = ErdTask.getters('nodesHistory')
  let newHistory = [nodeMap]
  /**
   * Push new data if the current index is the last item, otherwise,
   * override the history by concatenating the last item with the latest one
   */
  if (ErdTask.getters('activeHistoryIdx') === currentHistory.length - 1)
    newHistory = [...currentHistory, nodeMap]
  else if (currentHistory.at(-1)) newHistory = [currentHistory.at(-1), nodeMap]
  if (newHistory.length > 10) newHistory = newHistory.slice(1)
  setNodesHistory(newHistory)
}

export default {
  cascadeDelete,
  initEntities,
  updateActiveHistoryIdx,
  updateNodesHistory,
}
