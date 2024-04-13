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
import queryEditorService from '@/services/queryEditorService'
import erdTaskService from '@/services/erdTaskService'
import { uuidv1 } from '@/utils/helpers'

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
function insertBlankWke(fields = { worksheet_id: uuidv1(), name: 'WORKSHEET' }) {
  Worksheet.insert({ data: { id: fields.worksheet_id, name: fields.name } })
  WorksheetTmp.insert({ data: { id: fields.worksheet_id } })
  Worksheet.commit((state) => (state.active_wke_id = fields.worksheet_id))
}

/**
 * Insert a QueryEditor worksheet with its relational entities
 */
function insertQueryEditorWke() {
  const worksheet_id = uuidv1()
  insertBlankWke({ worksheet_id, name: 'QUERY EDITOR' })
  queryEditorService.insertQueryEditor(worksheet_id)
}

/**
 * @param {String} id - worksheet_id
 */
async function handleDeleteWke(id) {
  await cascadeDelete(id)
  //Auto insert a new blank wke
  if (Worksheet.all().length === 0) insertBlankWke()
}

export default {
  insertBlankWke,
  insertQueryEditorWke,
  handleDeleteWke,
}
