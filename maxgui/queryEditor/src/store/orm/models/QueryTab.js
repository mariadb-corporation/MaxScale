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
import { ORM_PERSISTENT_ENTITIES } from '@queryEditorSrc/store/config'
import { uuidv1 } from '@share/utils/helpers'
import queryHelper from '@queryEditorSrc/store/queryHelper'
import QueryResult from './QueryResult'
import QueryConn from './QueryConn'
import Editor from './Editor'
import QueryTabMem from './QueryTabMem'

export default class QueryTab extends Extender {
    static entity = ORM_PERSISTENT_ENTITIES.QUERY_TABS

    /**
     * If a record is deleted, then the corresponding records in the child
     * tables will be automatically deleted
     * @param {String|Function} payload - either a queryTab id or a callback function that return Boolean (filter)
     */
    static cascadeDelete(payload) {
        const entityIds = queryHelper.filterEntity(QueryTab, payload).map(entity => entity.id)
        entityIds.forEach(id => {
            QueryTab.delete(id) // delete itself

            // delete record in its the relational tables
            QueryTabMem.delete(id)
            Editor.delete(id)
            this.store().dispatch('fileSysAccess/deleteFileHandleData', id, { root: true })
            QueryResult.delete(id)
            QueryConn.delete(c => c.query_tab_id === id)
        })
    }

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return { name: this.string('Query Tab 1'), count: this.number(1) }
    }

    /**
     * Refresh non-key and non-relational fields of an entity and its relations
     * @param {String|Function} payload - either a QueryTab id or a callback function that return Boolean (filter)
     */
    static cascadeRefresh(payload) {
        const entityIds = queryHelper.filterEntity(QueryTab, payload).map(entity => entity.id)
        entityIds.forEach(id => {
            const target = QueryTab.query()
                .with('editor') // get editor relational field
                .whereId(id)
                .first()
            if (target) {
                // refresh its relations
                QueryTabMem.refresh(id)
                // keep query_txt data even after refresh all fields
                Editor.refresh(id, ['query_txt'])
                QueryResult.refresh(id)
            }
        })
    }

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            ...this.getNonKeyFields(),
            //FK
            worksheet_id: this.attr(null),
            // relationship fields
            editor: this.hasOne(Editor, 'id'),
            queryResult: this.hasOne(QueryResult, 'id'),
            queryTabMem: this.hasOne(QueryTabMem, 'id'),
        }
    }
}
