/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import QueryEditor from '../QueryEditor.vue'
import { lodash } from '@share/utils/helpers'

const stubModuleMethods = {
    handleAutoClearQueryHistory: () => null,
    validateConns: () => null,
}

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: false,
                component: QueryEditor,
                computed: {
                    is_validating_conn: () => false,
                    worksheets_arr: () => [{ id: 'WORKSHEET_123' }],
                    active_wke_id: () => 'WORKSHEET_123',
                    ctrDim: () => ({ width: 1280, height: 800 }),
                    getIsTxtEditor: () => true,
                },
                stubs: {
                    'sql-editor': "<div class='stub'></div>",
                    'readonly-sql-editor': "<div class='stub'></div>",
                },
            },
            opts
        )
    )
describe('QueryEditor', () => {
    let wrapper

    describe('QueryEditor created hook tests', () => {
        let handleAutoClearQueryHistoryCallCount = 0

        before(() => {
            mountFactory({
                shallow: true,
                methods: {
                    ...stubModuleMethods,
                    handleAutoClearQueryHistory: () => handleAutoClearQueryHistoryCallCount++,
                },
            })
        })
        it(`Should call 'handleAutoClearQueryHistory' action once when
        component is created`, () => {
            expect(handleAutoClearQueryHistoryCallCount).to.be.equals(1)
        })
    })

    it('Should pass accurate data to wke-ctr component via props', () => {
        wrapper = mountFactory()
        const wke = wrapper.findAllComponents({ name: 'wke-ctr' }).at(0)
        expect(wke.vm.$props.ctrDim).to.be.equals(wrapper.vm.ctrDim)
    })
})
