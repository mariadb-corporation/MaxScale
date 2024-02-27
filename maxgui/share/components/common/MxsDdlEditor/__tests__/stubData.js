/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import erdHelper from '@wsSrc/utils/erdHelper'
import { GENERATED_TYPES } from '@wsSrc/constants'

export const charsetCollationMapStub = {
    utf8mb4: {
        collations: ['utf8mb4_general_ci'],
        defCollation: 'utf8mb4_general_ci',
    },
}
const parsedTableStub = {
    defs: {
        col_map: {
            'col_6c423730-3d9e-11ee-ae7d-f7b5c34f152c': {
                name: 'id',
                data_type: 'INT(11)',
                un: false,
                zf: false,
                nn: true,
                charset: undefined,
                collate: undefined,
                generated: undefined,
                ai: false,
                default_exp: undefined,
                comment: undefined,
                id: 'col_6c423730-3d9e-11ee-ae7d-f7b5c34f152c',
            },
        },
        key_category_map: {
            'PRIMARY KEY': {
                'key_6c425e40-3d9e-11ee-ae7d-f7b5c34f152c': {
                    id: 'key_6c425e40-3d9e-11ee-ae7d-f7b5c34f152c',
                    cols: [{ id: 'col_6c423730-3d9e-11ee-ae7d-f7b5c34f152c' }],
                },
            },
        },
    },
    options: {
        engine: 'InnoDB',
        charset: 'utf8mb4',
        schema: 'test',
        name: 'table_1',
        collation: 'utf8mb4_general_ci',
    },
    id: 'tbl_1cb11b00-3d9e-11ee-bd9e-5944f4c72ef1',
}

export const editorDataStub = erdHelper.genDdlEditorData({
    parsedTable: parsedTableStub,
    charsetCollationMap: charsetCollationMapStub,
})

export const rowDataStub = [
    'col_6c423730-3d9e-11ee-ae7d-f7b5c34f152c',
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

export const generatedTypeItemsStub = Object.values(GENERATED_TYPES)

export const colKeyCategoryMapStub = { 'col_6c423730-3d9e-11ee-ae7d-f7b5c34f152c': ['PRIMARY KEY'] }

export const tableColNameMapStub = { 'col_6c423730-3d9e-11ee-ae7d-f7b5c34f152c': 'id' }

export const tableColMapStub = parsedTableStub.defs.col_map
