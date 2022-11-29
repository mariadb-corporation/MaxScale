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
import QueryResult from './QueryResult'
import QueryConn from './QueryConn'
import Editor from './Editor'

import { uuidv1 } from '@share/utils/helpers'

export default class QueryTab extends Model {
    static entity = 'queryTabs'

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
