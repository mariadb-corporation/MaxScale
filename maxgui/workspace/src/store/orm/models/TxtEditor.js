/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Extender from '@wsSrc/store/orm/Extender'
import { ORM_PERSISTENT_ENTITIES } from '@wsSrc/constants'

export default class TxtEditor extends Extender {
    static entity = ORM_PERSISTENT_ENTITIES.TXT_EDITORS

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            query_txt: this.string(''),
            is_vis_sidebar_shown: this.boolean(false),
        }
    }

    static fields() {
        return {
            id: this.attr(null),
            ...this.getNonKeyFields(),
        }
    }
}
