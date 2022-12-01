/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
import Editor from '@queryEditorSrc/store/orm/models/Editor'
import QueryResult from '@queryEditorSrc/store/orm/models/QueryResult'
import QueryTab from '@queryEditorSrc/store/orm/models/QueryTab'
import SchemaSidebar from '@queryEditorSrc/store/orm/models/SchemaSidebar'
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'
import QueryTabMem from '@queryEditorSrc/store/orm/models/QueryTabMem'
import WorksheetMem from '@queryEditorSrc/store/orm/models/WorksheetMem'

/**
 * Initialize a blank worksheet and its mandatory relational entities
 * @param {Object} [fields = { worksheet_id = uuidv1(), query_tab_id : uuidv1()}] - fields
 */
export function insertWke(fields = { worksheet_id: uuidv1(), query_tab_id: uuidv1() }) {
    Worksheet.insert({
        data: {
            id: fields.worksheet_id,
            name: 'WORKSHEET',
        },
    })
    Worksheet.commit(state => (state.active_wke_id = fields.worksheet_id))
    WorksheetMem.insert({ data: { id: fields.worksheet_id } })
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
    QueryTabMem.insert({ data: { id: fields.query_tab_id } })

    // update active_query_tab_map state
    QueryTab.commit(state => (state.active_query_tab_map[worksheet_id] = fields.query_tab_id))
}

/**
 * Initialize entities that will be kept only in memory for all worksheets and queryTabs
 */
function initMemEntities() {
    const worksheets = Worksheet.query()
        .with('queryTabs')
        .all()
    worksheets.forEach(w => {
        WorksheetMem.insert({ data: { id: w.id } })
        w.queryTabs.forEach(t => QueryTabMem.insert({ data: { id: t.id } }))
    })
}
export default () => {
    if (Worksheet.all().length === 0) insertWke()
    else initMemEntities()
}
