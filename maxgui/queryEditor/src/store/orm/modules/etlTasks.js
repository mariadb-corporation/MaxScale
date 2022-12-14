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
import EtlTask from '@queryEditorSrc/store/orm/models/EtlTask'

export default {
    namespaced: true,
    actions: {
        insertEtlTask(_, name) {
            EtlTask.insert({
                data: {
                    name:
                        name ||
                        `ETL - ${this.vue.$helpers.dateFormat({
                            value: new Date(),
                            formatType: 'DATE_RFC2822',
                        })}`,
                },
            })
        },
        //TODO: add cancel ETL task action
    },
}
