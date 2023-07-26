/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Extender from '@wsSrc/store/orm/Extender'
import { ORM_PERSISTENT_ENTITIES, ORM_TMP_ENTITIES } from '@wsSrc/store/config'
import { LINK_SHAPES } from '@share/components/common/MxsSvgGraphs/shapeConfig'
import { uuidv1 } from '@share/utils/helpers'

export default class ErdTask extends Extender {
    static entity = ORM_PERSISTENT_ENTITIES.ERD_TASKS

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            nodes: this.attr([]),
            count: this.number(1),
            graph_config: this.attr({
                link: { isAttrToAttr: false },
                linkShape: { type: LINK_SHAPES.ORTHO },
            }),
            is_laid_out: this.boolean(false), //conditionally skip the simulation
        }
    }

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            ...this.getNonKeyFields(),
            connection: this.hasOne(ORM_PERSISTENT_ENTITIES.QUERY_CONNS, 'erd_task_id'),
            erdTaskTmp: this.hasOne(ORM_TMP_ENTITIES.ERD_TASKS_TMP, 'id'),
        }
    }
}
