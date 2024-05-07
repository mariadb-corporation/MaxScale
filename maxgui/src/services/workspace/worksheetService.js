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
import WorksheetTmp from '@wsModels/WorksheetTmp'
import QueryConn from '@wsModels/QueryConn'
import QueryTab from '@wsModels/QueryTab'
import queryEditorService from '@wsServices/queryEditorService'
import erdTaskService from '@wsServices/erdTaskService'
import { uuidv1 } from '@/utils/helpers'
import { t as typy } from 'typy'

/**
 * @param {string} field - name of the task id field
 * @param {string} taskId
 * @returns {string} wkeId
 */
function findWkeIdByTaskId(field, taskId) {
  return typy(Worksheet.query().where(field, taskId).first(), 'id').safeString
}

/**
 * @param {string} id - wkeId or query_editor_id
 * @returns {object} axios request config
 */
function findRequestConfig(id) {
  return typy(WorksheetTmp.find(id), 'request_config').safeObjectOrEmpty
}

function findEtlTaskWkeId(id) {
  return findWkeIdByTaskId('etl_task_id', id)
}

function findErdTaskWkeId(id) {
  return findWkeIdByTaskId('erd_task_id', id)
}

function findEtlTaskRequestConfig(id) {
  return findRequestConfig(findEtlTaskWkeId(id))
}

function findConnRequestConfig(id) {
  const { etl_task_id, query_tab_id, query_editor_id, erd_task_id } = typy(
    QueryConn.find(id)
  ).safeObjectOrEmpty
  if (etl_task_id) return findRequestConfig(findEtlTaskWkeId(etl_task_id))
  else if (erd_task_id) return findRequestConfig(findErdTaskWkeId(erd_task_id))
  else if (query_editor_id) return findRequestConfig(query_editor_id)
  else if (query_tab_id)
    return findRequestConfig(typy(QueryTab.find(query_tab_id), 'query_editor_id').safeString)
  return {}
}

/**
 * If a record is deleted, then the corresponding records in its relational
 * tables will be automatically deleted
 * @param {String|Function} payload - either a worksheet id or a callback function that return Boolean (filter)
 */
async function cascadeDelete(payload) {
  const entityIds = Worksheet.filterEntity(Worksheet, payload).map((entity) => entity.id)
  for (const id of entityIds) {
    const { erd_task_id, query_editor_id } = Worksheet.find(id) || {}
    if (erd_task_id) await erdTaskService.cascadeDelete(erd_task_id)
    if (query_editor_id) await queryEditorService.cascadeDelete(query_editor_id)
    WorksheetTmp.delete(id)
    Worksheet.delete(id) // delete itself
    // Auto select the last Worksheet as the active one
    if (Worksheet.getters('activeId') === id) {
      const lastWke = Worksheet.query().last()
      if (lastWke) Worksheet.commit((state) => (state.active_wke_id = lastWke.id))
    }
  }
}

/**
 * Initialize a blank worksheet
 * @param {Object} [fields = { worksheet_id: uuidv1(), name: 'WORKSHEET'}] - fields
 */
function insertBlank(fields = { worksheet_id: uuidv1(), name: 'WORKSHEET' }) {
  Worksheet.insert({ data: { id: fields.worksheet_id, name: fields.name } })
  WorksheetTmp.insert({ data: { id: fields.worksheet_id } })
  Worksheet.commit((state) => (state.active_wke_id = fields.worksheet_id))
}

/**
 * Insert a QueryEditor worksheet with its relational entities
 */
function insertQueryEditor() {
  const worksheet_id = uuidv1()
  insertBlank({ worksheet_id, name: 'QUERY EDITOR' })
  queryEditorService.insert(worksheet_id)
}

/**
 * @param {String} id - worksheet_id
 */
async function handleDelete(id) {
  await cascadeDelete(id)
  //Auto insert a new blank wke
  if (Worksheet.all().length === 0) insertBlank()
}

export default {
  findEtlTaskWkeId,
  findRequestConfig,
  findEtlTaskRequestConfig,
  findConnRequestConfig,
  insertBlank,
  insertQueryEditor,
  handleDelete,
}
