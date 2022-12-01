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
 * Initialize default entities which will be persisted in indexedDB
 */
function initDefEntities() {
    const worksheet_id = uuidv1()
    const query_tab_id = uuidv1()

    Worksheet.create({
        data: {
            id: worksheet_id,
            name: 'WORKSHEET',
            schemaSidebar: new SchemaSidebar(),
        },
    })
    QueryTab.create({
        data: {
            id: query_tab_id,
            worksheet_id,
            editor: new Editor(),
            queryResult: new QueryResult(),
        },
    })
    Worksheet.commit(state => (state.active_wke_id = worksheet_id))
    // update active_query_tab_map state
    QueryTab.commit(state => (state.active_query_tab_map[worksheet_id] = query_tab_id))
}

/**
 * Initialize entities that will be kept only in memory
 */
function initMemEntities() {
    const worksheets = Worksheet.query()
        .with('queryTabs')
        .all()
    worksheets.forEach(w => {
        WorksheetMem.create({ data: { id: w.id } })
        w.queryTabs.forEach(t => QueryTabMem.create({ data: { id: t.id } }))
    })
}
export default () => {
    if (Worksheet.all().length === 0) initDefEntities()
    initMemEntities()
}
