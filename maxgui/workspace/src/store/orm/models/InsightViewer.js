/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Extender from '@wsSrc/store/orm/Extender'
import { ORM_PERSISTENT_ENTITIES } from '@wsSrc/constants'

export default class InsightViewer extends Extender {
    static entity = ORM_PERSISTENT_ENTITIES.INSIGHT_VIEWERS

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            active_spec: this.string(''),
            active_node: this.attr(null),
        }
    }

    static fields() {
        return {
            id: this.attr(null),
            ...this.getNonKeyFields(),
        }
    }
}
