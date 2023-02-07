/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { uuidv1 } from '@share/utils/helpers'
import Editor from '@wsModels/Editor'
import QueryResult from '@wsModels/QueryResult'
import QueryTab from '@wsModels/QueryTab'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import Worksheet from '@wsModels/Worksheet'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'

/**
 * Initialize a blank worksheet
 * @param {Object} [fields = { worksheet_id = uuidv1(), query_tab_id : uuidv1()}] - fields
 */
export function insertBlankWke(
    fields = { worksheet_id: uuidv1(), query_tab_id: uuidv1(), name: 'WORKSHEET' }
) {
    Worksheet.insert({ data: { id: fields.worksheet_id, name: fields.name } })
    Worksheet.commit(state => (state.active_wke_id = fields.worksheet_id))
}

/**
 * Initialize a worksheet with and its query editor relational entities
 * @param {Object} [fields = { worksheet_id = uuidv1(), query_tab_id : uuidv1()}] - fields
 */
export function insertQueryEditor(
    fields = { worksheet_id: uuidv1(), query_tab_id: uuidv1(), name: 'QUERY EDITOR' }
) {
    insertBlankWke(fields)
    QueryEditorTmp.insert({ data: { id: fields.worksheet_id } })
    SchemaSidebar.insert({ data: { id: fields.worksheet_id } })
    insertQueryTab(fields.worksheet_id, { query_tab_id: fields.query_tab_id })
}

/**
 * Initialize a blank QueryTab and its mandatory relational entities
 * @param {String} worksheet_id  - id of the worksheet has QueryTab being inserted
 * @param {Object} [ fields = { query_tab_id : uuidv1() } ] - fields
 */
export function insertQueryTab(worksheet_id, fields = { query_tab_id: uuidv1() }) {
    let name = 'Query Tab 1',
        count = 1

    const lastQueryTabOfWke = QueryTab.query()
        .where(t => t.worksheet_id === worksheet_id)
        .last()

    if (lastQueryTabOfWke) {
        count = lastQueryTabOfWke.count + 1
        name = `Query Tab ${count}`
    }
    QueryTab.insert({
        data: {
            id: fields.query_tab_id,
            count,
            name,
            worksheet_id,
            ...fields,
        },
    })
    Editor.insert({ data: { id: fields.query_tab_id } })

    QueryResult.insert({ data: { id: fields.query_tab_id } })
    QueryTabTmp.insert({ data: { id: fields.query_tab_id } })
    Worksheet.update({
        where: Worksheet.getters('getActiveWkeId'),
        data: { active_query_tab_id: fields.query_tab_id },
    })
}

/**
 * Initialize entities that will be kept only in memory for all worksheets and queryTabs
 */
function initMemEntities() {
    const worksheets = Worksheet.query()
        .with('queryTabs')
        .all()
    worksheets.forEach(w => {
        if (w.active_query_tab_id) {
            QueryEditorTmp.insert({ data: { id: w.id } })
            w.queryTabs.forEach(t => QueryTabTmp.insert({ data: { id: t.id } }))
        } else if (w.active_etl_task_id) {
            //Init mem entities for ETL
        }
    })
}
export default () => {
    if (Worksheet.all().length === 0) insertBlankWke()
    else initMemEntities()
}
