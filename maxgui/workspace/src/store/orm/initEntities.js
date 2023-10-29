/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { uuidv1 } from '@share/utils/helpers'
import Editor from '@wsModels/Editor'
import EtlTask from '@wsModels/EtlTask'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import QueryEditor from '@wsModels/QueryEditor'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import QueryTab from '@wsModels/QueryTab'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import QueryResult from '@wsModels/QueryResult'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import Worksheet from '@wsModels/Worksheet'
import WorksheetTmp from '@wsModels/WorksheetTmp'

/**
 * Initialize a blank worksheet
 * @param {Object} [fields = { worksheet_id: uuidv1(), name: 'WORKSHEET'}] - fields
 */
export function insertBlankWke(fields = { worksheet_id: uuidv1(), name: 'WORKSHEET' }) {
    Worksheet.insert({ data: { id: fields.worksheet_id, name: fields.name } })
    WorksheetTmp.insert({ data: { id: fields.worksheet_id } })
    Worksheet.commit(state => (state.active_wke_id = fields.worksheet_id))
}

/**
 * Insert a QueryEditor worksheet with its relational entities
 */
export function insertQueryEditorWke() {
    const worksheet_id = uuidv1()
    insertBlankWke({ worksheet_id, name: 'QUERY EDITOR' })
    insertQueryEditor(worksheet_id)
}

/**
 * Insert a QueryEditor with its relational entities
 * @param {String} query_editor_id - QueryEditor id
 */
export function insertQueryEditor(query_editor_id) {
    QueryEditor.insert({ data: { id: query_editor_id } })
    QueryEditorTmp.insert({ data: { id: query_editor_id } })
    SchemaSidebar.insert({ data: { id: query_editor_id } })
    insertQueryTab(query_editor_id)
}

/**
 * Initialize a blank QueryTab and its mandatory relational entities
 * @param {String} query_editor_id  - id of the QueryEditor has QueryTab being inserted
 * @param {Object} [ fields = { query_tab_id: uuidv1(), name: '' } ] - fields
 */
export function insertQueryTab(query_editor_id, fields = { query_tab_id: uuidv1(), name: '' }) {
    let tabName = 'Query Tab 1',
        count = 1

    const lastQueryTabOfWke = QueryTab.query()
        .where(t => t.query_editor_id === query_editor_id)
        .last()
    if (lastQueryTabOfWke) {
        count = lastQueryTabOfWke.count + 1
        tabName = `Query Tab ${count}`
    }
    if (fields.name) tabName = fields.name
    QueryTab.insert({
        data: {
            id: fields.query_tab_id,
            count,
            name: tabName,
            query_editor_id,
        },
    })
    Editor.insert({ data: { id: fields.query_tab_id } })
    QueryResult.insert({ data: { id: fields.query_tab_id } })
    QueryTabTmp.insert({ data: { id: fields.query_tab_id } })
    QueryEditor.update({
        where: query_editor_id,
        data: { active_query_tab_id: fields.query_tab_id },
    })
}

/**
 * Initialize a blank EtlTask and its mandatory relational entities
 * @param {String} param.id - etl task id
 * @param {String} param.name - etl task name
 */
export function insertEtlTask({ id, name }) {
    EtlTask.insert({ data: { id, name, created: Date.now() } })
    EtlTaskTmp.insert({ data: { id } })
}

/**
 * Initialize entities that will be kept only in memory for all worksheets and queryTabs
 */
function initMemEntities() {
    const worksheets = Worksheet.all()
    worksheets.forEach(w => {
        WorksheetTmp.insert({ data: { id: w.id } })
        if (w.query_editor_id) {
            const queryEditor = QueryEditor.query()
                .where('id', w.query_editor_id)
                .with('queryTabs')
                .first()
            QueryEditorTmp.insert({ data: { id: queryEditor.id } })
            queryEditor.queryTabs.forEach(t => QueryTabTmp.insert({ data: { id: t.id } }))
        } else if (w.etl_task_id) EtlTaskTmp.insert({ data: { id: w.etl_task_id } })
    })
}
export default () => {
    if (Worksheet.all().length === 0) insertBlankWke()
    else initMemEntities()
}
