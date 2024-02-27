/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 *  Public License.
 */

import mount from '@tests/unit/setup'
import RowLimit from '../RowLimit'
import { getErrMsgEle } from '@tests/unit/utils'

const mountFactory = opts =>
    mount({
        shallow: false,
        component: RowLimit,
        ...opts,
    })

describe(`row-limit - created hook and data communication tests`, () => {
    let wrapper
    it(`Should pass accurate data to v-combobox via props`, () => {
        const DEF_ROW_LIMIT_OPTS = [10, 50, 100]
        const mockvalue = 1000
        wrapper = mountFactory({
            shallow: true,
            propsData: { value: mockvalue },
            attrs: { items: DEF_ROW_LIMIT_OPTS },
        })
        const dropdown = wrapper.findComponent({ name: 'v-combobox' })
        const { value, items } = dropdown.vm.$props
        expect(value).to.be.equals(mockvalue)
        expect(items).to.be.deep.equals(DEF_ROW_LIMIT_OPTS)
    })
    describe(`Input validation tests`, () => {
        let wrapper, dropdown
        beforeEach(() => {
            wrapper = mountFactory({ propsData: { hideDetails: 'auto' } })
            dropdown = wrapper.findComponent({ name: 'v-combobox' })
        })
        it(`Should show required error message if input is empty`, async () => {
            await wrapper.vm.onInput({ srcElement: { value: '' } })
            wrapper.vm.$nextTick(() => {
                expect(getErrMsgEle(dropdown).text()).to.be.equals(
                    wrapper.vm.$mxs_t('errors.requiredInput', {
                        inputName: wrapper.vm.$mxs_t('rowLimit'),
                    })
                )
            })
        })
        it(`Should show non integer error message if input is a string`, async () => {
            await wrapper.vm.onInput({ srcElement: { value: 'abc' } })
            wrapper.vm.$nextTick(() => {
                expect(getErrMsgEle(dropdown).text()).to.be.equals(
                    wrapper.vm.$mxs_t('errors.nonInteger')
                )
            })
        })
        it(`Should show 'largerThanZero' if input is zero`, async () => {
            await wrapper.vm.onInput({ srcElement: { value: 0 } })
            wrapper.vm.$nextTick(() => {
                expect(getErrMsgEle(dropdown).text()).to.be.equals(
                    wrapper.vm.$mxs_t('errors.largerThanZero')
                )
            })
        })
    })
})
