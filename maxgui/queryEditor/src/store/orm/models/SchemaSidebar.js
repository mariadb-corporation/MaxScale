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

export default class SchemaSidebar extends Model {
    static entity = 'schemaSidebars'

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            search_schema: this.string(''),
            expanded_nodes: this.attr([]),
            //FK
            worksheet_id: this.attr(null),
        }
    }
}
