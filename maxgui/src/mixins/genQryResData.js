/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    methods: {
        genHeaders(data) {
            if (!data.columns) return []
            return data.columns.map((col, index) => ({
                text: col.name,
                value: `field_${index}`,
            }))
        },
        genRows(data) {
            if (!data.rowset) return []
            return data.rowset.map(set => {
                let obj = {}
                for (const [i, v] of set.entries()) {
                    obj = { ...obj, [`field_${i}`]: v }
                }
                return obj
            })
        },
    },
}
