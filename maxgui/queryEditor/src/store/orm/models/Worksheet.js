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
import { Model } from '@vuex-orm/core'
import { uuidv1 } from '@share/utils/helpers'
import { ORM_PERSISTENT_ENTITIES } from '@queryEditorSrc/store/config'
import queryHelper from '@queryEditorSrc/store/queryHelper'
import SchemaSidebar from './SchemaSidebar'
import QueryTab from './QueryTab'
import QueryConn from './QueryConn'
import WorksheetMem from './WorksheetMem'

export default class Worksheet extends Model {
    static entity = ORM_PERSISTENT_ENTITIES.WORKSHEETS

    static state() {
        return {
            active_wke_id: null, // Persistence
        }
    }

    /**
     * If a record in the parent table (worksheet) is deleted, then the corresponding records in the child
     * tables (schemaSidebar, queryConn, queryTab) will automatically be deleted
     * @param {String|Function} payload - either a worksheet id or a callback function that return Boolean (filter)
     */
    static cascadeDelete(payload) {
        const modelIds = queryHelper.filterEntity(Worksheet, payload).map(model => model.id)
        modelIds.forEach(id => {
            Worksheet.delete(w => w.id === id) // delete itself
            WorksheetMem.delete(m => m.worksheet_id === id)

            // delete records in the child tables
            SchemaSidebar.delete(s => s.worksheet_id === id)
            QueryConn.delete(c => c.worksheet_id === id)
            QueryTab.cascadeDelete(t => t.worksheet_id === id)
            // update active_query_tab_map state
            QueryTab.commit(state => delete state.active_query_tab_map[id]) // delete worksheet key
        })
    }

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return { name: this.string('WORKSHEET') }
    }

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            ...this.getNonKeyFields(),
            // relationship fields
            queryTabs: this.hasMany(QueryTab, 'worksheet_id'),
            schemaSidebar: this.hasOne(SchemaSidebar, 'worksheet_id'),
            queryConn: this.hasOne(QueryConn, 'worksheet_id'),
            worksheetMem: this.hasOne(WorksheetMem, 'worksheet_id'),
        }
    }
}
