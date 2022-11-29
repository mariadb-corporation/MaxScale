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
import { ORM_PERSISTENT_ENTITIES, QUERY_MODES } from '@queryEditorSrc/store/config'
import { uuidv1 } from '@share/utils/helpers'

export default class QueryResult extends Model {
    static entity = ORM_PERSISTENT_ENTITIES.QUERY_RESULTS

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            curr_query_mode: this.string(QUERY_MODES.QUERY_VIEW),
            show_vis_sidebar: this.boolean(false),
            //FK
            query_tab_id: this.attr(null),
        }
    }
}
