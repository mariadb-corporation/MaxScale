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
import erdHelper from '@wsSrc/utils/erdHelper'
import { tableParser } from '@wsSrc/utils/helpers'
import tableTemplate from '@wkeComps/ErdWke/tableTemplate'

export const charsetCollationMapStub = {
    utf8mb4: {
        collations: ['utf8mb4_general_ci'],
        defCollation: 'utf8mb4_general_ci',
    },
}

export const editorDataStub = erdHelper.genDdlEditorData({
    parsedTable: tableParser.parse({
        ddl: tableTemplate('table_1'),
        schema: 'test',
        autoGenId: true,
    }),
    charsetCollationMap: charsetCollationMapStub,
})

export const rowDataStub = [
    'col_74dcf0f0-3cc3-11ee-8a1e-f377468c3f6a',
    'id',
    'INT(11)',
    true,
    true,
    false,
    false,
    false,
    false,
    '(none)',
    null,
    '',
    '',
    '',
]
