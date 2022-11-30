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
import QueryTab from './QueryTab'
import Worksheet from './Worksheet'

export default class QueryConn extends Extender {
    static entity = ORM_PERSISTENT_ENTITIES.QUERY_CONNS

    /**
     * If a record is deleted, then the corresponding records in its relational
     * tables (Worksheet, QueryTab) will have their data refreshed
     * @param {String|Function} payload - either a QueryConn id or a callback function that return Boolean (filter)
     */
    static cascadeRefreshOnDelete(payload) {
        const entities = queryHelper.filterEntity(QueryConn, payload)
        entities.forEach(entity => {
            /**
             * refresh its relations, when a connection bound to the worksheet is deleted,
             * all QueryTabs data should also be refreshed (Worksheet.cascadeRefresh).
             * If the connection being deleted doesn't have worksheet_id FK but query_tab_id FK,
             * it is a connection bound to QueryTab, thus call QueryTab.cascadeRefresh.
             */
            if (entity.worksheet_id) Worksheet.cascadeRefresh(w => w.id === entity.worksheet_id)
            else if (entity.query_tab_id) QueryTab.cascadeRefresh(t => t.id === entity.query_tab_id)
            QueryConn.delete(entity.id) // delete itself
        })
    }

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            active_db: this.string(''),
            attributes: this.attr({}),
            binding_type: this.string(''),
            name: this.string(''),
            type: this.string(''),
            meta: this.attr({}),
            clone_of_conn_id: this.attr(null).nullable(),
        }
    }

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            ...this.getNonKeyFields(),
            //FK, one to one inverse
            query_tab_id: this.attr(null).nullable(),
            worksheet_id: this.attr(null).nullable(),
            queryTab: this.belongsTo(QueryTab, 'query_tab_id'),
            worksheet: this.belongsTo(Worksheet, 'worksheet_id'),
        }
    }
}
