/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export const prvw_data = {
    data: {
        queryId: '019b179-0000-1523-0000-5789000140bla',
        queryText: 'SELECT * FROM mysql.help_keyword_id;',
        fields: new Array(20).fill(null).map((item, i) => `col-${i}`),
        data: new Array(10000)
            .fill(null)
            .map(() => new Array(20).fill(null).map((item, i) => `cell-${i}`)),
    },
}

export const prvw_data_details = {
    data: {
        queryId: '019bc179-0000-1523-0000-5789000140de',
        queryText: 'DESCRIBE mysql.help_keyword_id;',
        fields: new Array(20).fill(null).map((item, i) => `col-${i}`),
        data: new Array(10000)
            .fill(null)
            .map(() => new Array(20).fill(null).map((item, i) => `cell-${i}`)),
    },
}
