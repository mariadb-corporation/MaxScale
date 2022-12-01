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
import Extender from '@queryEditorSrc/store/orm/Extender'
import { uuidv1 } from '@share/utils/helpers'
import { ORM_PERSISTENT_ENTITIES } from '@queryEditorSrc/store/config'
import queryHelper from '@queryEditorSrc/store/queryHelper'
import SchemaSidebar from './SchemaSidebar'
import QueryTab from './QueryTab'
import QueryConn from './QueryConn'
import WorksheetMem from './WorksheetMem'

export default class Worksheet extends Extender {
    static entity = ORM_PERSISTENT_ENTITIES.WORKSHEETS

    static state() {
        return {
            active_wke_id: null, // Persistence
        }
    }

    /**
     * If a record is deleted, then the corresponding records in its relational
     * tables will be automatically deleted
     * @param {String|Function} payload - either a worksheet id or a callback function that return Boolean (filter)
     */
    static cascadeDelete(payload) {
        const entityIds = queryHelper.filterEntity(Worksheet, payload).map(entity => entity.id)
        entityIds.forEach(id => {
            Worksheet.delete(id) // delete itself
            // delete records in its relational tables
            WorksheetMem.delete(id)
            SchemaSidebar.delete(id)
            QueryConn.delete(c => c.worksheet_id === id)
            QueryTab.cascadeDelete(t => t.worksheet_id === id)
        })
    }

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return { name: this.string('WORKSHEET') }
    }

    /**
     * Refresh non-key and non-relational fields of an entity and its relations
     * @param {String|Function} payload - either a Worksheet id or a callback function that return Boolean (filter)
     */
    static cascadeRefresh(payload) {
        const entityIds = queryHelper.filterEntity(Worksheet, payload).map(entity => entity.id)
        entityIds.forEach(id => {
            Worksheet.refresh(id) // refresh itself
            // refresh its relations
            WorksheetMem.refresh(id)
            SchemaSidebar.refresh(id)
            // refresh all queryTabs and its relations
            QueryTab.cascadeRefresh(t => t.worksheet_id === id)
        })
    }

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            ...this.getNonKeyFields(),
            // relationship fields
            queryTabs: this.hasMany(QueryTab, 'worksheet_id'),
            schemaSidebar: this.hasOne(SchemaSidebar, 'id'),
            worksheetMem: this.hasOne(WorksheetMem, 'id'),
        }
    }
}
