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
import { ORM_PERSISTENT_ENTITIES } from '@queryEditorSrc/store/config'
import { uuidv1 } from '@share/utils/helpers'
import queryHelper from '@queryEditorSrc/store/queryHelper'
import QueryResult from './QueryResult'
import QueryConn from './QueryConn'
import Editor from './Editor'

export default class QueryTab extends Model {
    static entity = ORM_PERSISTENT_ENTITIES.QUERY_TABS

    /**
     * If a record in the parent table (queryTab) is deleted, then the corresponding records in the child
     * tables (queryResult, queryConn, editor) will automatically be deleted
     * @param {String|Function} payload - either a queryTab id or a callback function that return Boolean (filter)
     */
    static cascadeDelete(payload) {
        const modelIds = queryHelper.filterEntity(QueryTab, payload).map(model => model.id)
        modelIds.forEach(id => {
            QueryTab.delete(t => t.id === id) // delete itself
            // delete records in the child tables
            Editor.delete(e => e.query_tab_id === id)
            QueryResult.delete(r => r.query_tab_id === id)
            QueryConn.delete(c => c.query_tab_id === id)
        })
    }

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            name: this.string('Query Tab 1'),
            count: this.number(1),
            //FK
            worksheet_id: this.attr(null),
            // relationship fields
            editor: this.hasOne(Editor, 'query_tab_id'),
            queryResult: this.hasOne(QueryResult, 'query_tab_id'),
            queryConn: this.hasOne(QueryConn, 'query_tab_id'),
        }
    }
}
