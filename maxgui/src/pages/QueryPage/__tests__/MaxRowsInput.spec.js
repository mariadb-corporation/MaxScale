/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 *  Public License.
 */

import mount from '@tests/unit/setup'
import MaxRowsInput from '@/pages/QueryPage/MaxRowsInput'

const mountFactory = opts =>
    mount({
        shallow: false,
        component: MaxRowsInput,
        ...opts,
    })

describe(`MaxRowsInput`, () => {
    let wrapper
    it(`Should pass accurate data to max-rows-dropdown via props`, () => {
        wrapper = mountFactory({ shallow: true, computed: { maxRows: () => 1000 } })
        const dropdown = wrapper.findComponent({ name: 'v-combobox' })
        const { value, items } = dropdown.vm.$props
        expect(value).to.be.equals(wrapper.vm.maxRows)
        expect(items).to.be.deep.equals(wrapper.vm.SQL_DEF_MAX_ROWS_OPTS)
    })
})
//TODO: add more unit tests
