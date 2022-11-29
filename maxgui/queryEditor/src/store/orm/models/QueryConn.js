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
import QueryTab from './QueryTab'
import Worksheet from './Worksheet'

export default class QueryConn extends Model {
    static entity = ORM_PERSISTENT_ENTITIES.QUERY_CONNS

    /**
     * If a record in this table (QueryConn) is deleted, then the corresponding records in the relational
     * tables (Worksheet, QueryTab) will have the relational fields set to NULL.
     * @param {String|Function} payload - either a queryConn id or a callback function that return Boolean (filter)
     */
    static deleteSetNull(payload) {
        const models = queryHelper.filterEntity(QueryConn, payload)
        models.forEach(model => {
            QueryConn.delete(c => c.id === model.id) // delete itself
            // set relational fields to null for its relational tables
            Worksheet.update({
                where: w => w.id === model.worksheet_id,
                data: { queryConn: null },
            })
            QueryTab.update({
                where: t => t.id === model.query_tab_id,
                data: { queryConn: null },
            })
        })
    }

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            sql_conn: this.attr(null), // stores data of a sql connection from API
            active_db: this.string(''),
            //FK: a connection can be bound to either a QueryTab or a Worksheet, so one of them is nullable
            query_tab_id: this.attr(null).nullable(),
            worksheet_id: this.attr(null).nullable(),
        }
    }
}
