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
import { getErrMsgEle } from '@tests/unit/utils'

const mountFactory = opts =>
    mount({
        shallow: false,
        component: MaxRowsInput,
        ...opts,
    })

describe(`MaxRowsInput - created hook and data communication tests`, () => {
    let wrapper
    it(`Should call updateMaxRows method on created hook`, () => {
        const fnSpy = sinon.spy(MaxRowsInput.methods, 'updateMaxRows')
        wrapper = mountFactory({ shallow: true })
        fnSpy.should.have.been.calledOnce
        fnSpy.restore()
    })
    it(`Should pass accurate data to max-rows-dropdown via props`, () => {
        wrapper = mountFactory({ shallow: true })
        const dropdown = wrapper.findComponent({ name: 'v-combobox' })
        const { value, items, hideDetails } = dropdown.vm.$props
        expect(value).to.be.equals(wrapper.vm.maxRows)
        expect(items).to.be.deep.equals(wrapper.vm.SQL_DEF_MAX_ROWS_OPTS)
        expect(hideDetails).to.be.equals(wrapper.vm.$props.hideDetails)
    })
    it(`Should conditionally add 'max-rows-dropdown--fieldset-border' class`, async () => {
        const styleClass = 'max-rows-dropdown--fieldset-border'
        wrapper = mountFactory({ shallow: true })
        const dropdown = wrapper.findComponent({ name: 'v-combobox' })
        expect(dropdown.classes().includes(styleClass)).to.be.true
        await wrapper.setProps({ hasFieldsetBorder: false })
        expect(dropdown.classes().includes(styleClass)).to.be.false
    })
})
describe(`MaxRowsInput- input validation tests`, () => {
    let wrapper, dropdown
    beforeEach(() => {
        wrapper = mountFactory({ propsData: { hideDetails: 'auto' } })
        dropdown = wrapper.findComponent({ name: 'v-combobox' })
    })
    it(`Should show required error message if input is empty`, async () => {
        await wrapper.vm.onInput({ srcElement: { value: '' } })
        wrapper.vm.$nextTick(() => {
            expect(getErrMsgEle(dropdown).text()).to.be.equals(
                wrapper.vm.$t('errors.requiredInput', { inputName: wrapper.vm.$t('maxRows') })
            )
        })
    })
    it(`Should show non integer error message if input is a string`, async () => {
        await wrapper.vm.onInput({ srcElement: { value: 'abc' } })
        wrapper.vm.$nextTick(() => {
            expect(getErrMsgEle(dropdown).text()).to.be.equals(wrapper.vm.$t('errors.nonInteger'))
        })
    })
    it(`Should show 'largerThanZero' if input is string`, async () => {
        await wrapper.vm.onInput({ srcElement: { value: -1 } })
        wrapper.vm.$nextTick(() => {
            expect(getErrMsgEle(dropdown).text()).to.be.equals(
                wrapper.vm.$t('errors.largerThanZero')
            )
        })
    })
})
describe(`MaxRowsInput- method and other tests`, () => {
    let wrapper
    //updateMaxRows is called automatically when component is mounted
    it(`Should update maxRows when updateMaxRows is called`, () => {
        const dummy_query_max_rows = 10
        wrapper = mountFactory({ computed: { query_max_rows: () => dummy_query_max_rows } })
        expect(wrapper.vm.$data.maxRows).to.be.deep.equals({
            text: dummy_query_max_rows,
            value: dummy_query_max_rows,
        })
    })
    it(`Should render "Don't Limit" if max_rows value is 0`, () => {
        const dummy_query_max_rows = 0
        wrapper = mountFactory({ computed: { query_max_rows: () => dummy_query_max_rows } })
        expect(wrapper.vm.$data.maxRows).to.be.deep.equals({
            text: `Don't Limit`,
            value: dummy_query_max_rows,
        })
    })
})
