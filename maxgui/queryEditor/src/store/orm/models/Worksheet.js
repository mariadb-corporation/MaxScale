/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-10-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { Model } from '@vuex-orm/core'
import { uuidv1 } from '@share/utils/helpers'
import SchemaSidebar from './SchemaSidebar'
import QueryTab from './QueryTab'
import QueryConn from './QueryConn'

export default class Worksheet extends Model {
    static entity = 'worksheets'

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            name: this.string('WORKSHEET'),
            // relationship fields
            queryTabs: this.hasMany(QueryTab, 'worksheet_id'),
            schemaSidebar: this.hasOne(SchemaSidebar, 'worksheet_id'),
            queryConn: this.hasOne(QueryConn, 'worksheet_id'),
        }
    }
}
