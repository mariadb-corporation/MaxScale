/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import QueryConfigDialog from '@/pages/QueryPage/QueryConfigDialog'
import { inputChangeMock } from '@tests/unit/utils'

const mountFactory = opts =>
    mount({
        shallow: true,
        component: QueryConfigDialog,
        ...opts,
    })
/**
 * a mock to change a config value
 */
async function mockChangingConfig({ wrapper, key, value }) {
    await wrapper.setData({
        config: { ...wrapper.vm.config, [key]: value },
    })
}
describe(`QueryConfigDialog - child component's data communication tests `, () => {
    it(`Should pass accurate data to base-dialog via props`, () => {
        let wrapper = mountFactory()
        const dlg = wrapper.findComponent({ name: 'base-dialog' })
        const { value, title, onSave, lazyValidation, hasChanged } = dlg.vm.$props
        expect(value).to.be.equals(wrapper.vm.isOpened)
        expect(title).to.be.equals(wrapper.vm.$t('queryConfig'))
        expect(onSave).to.be.equals(wrapper.vm.onSave)
        expect(lazyValidation).to.be.false
        expect(hasChanged).to.be.equals(wrapper.vm.hasChanged)
    })
})

describe(`QueryConfigDialog - tests after dialog is opened `, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory()
    })
    it(`Should call setCurCnf after dialog is opened`, async () => {
        const setCurrCnfSpy = sinon.spy(wrapper.vm, 'setCurCnf')
        await wrapper.setProps({ value: true }) // open dialog
        setCurrCnfSpy.should.have.been.calledOnce
    })
    it(`Should deep copy curCnf to config after dialog is opened `, async () => {
        await wrapper.setProps({ value: true }) // open dialog
        expect(wrapper.vm.config).to.be.deep.equals(wrapper.vm.curCnf)
        expect(wrapper.vm.curCnf.maxRows).to.be.not.equals(1)
        await mockChangingConfig({ wrapper, key: 'maxRows', value: 1 })
        // change to `config` shouldn't affect curCnf
        expect(wrapper.vm.curCnf.maxRows).to.be.not.equals(1)
    })
    it(`Should return accurate value for hasChanged`, async () => {
        await wrapper.setProps({ value: true }) // open dialog
        expect(wrapper.vm.hasChanged).to.be.false // no changes to form yet
        await mockChangingConfig({ wrapper, key: 'maxRows', value: 1 })
        expect(wrapper.vm.hasChanged).to.be.true
    })
})

describe(`QueryConfigDialog - form input tests`, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory({ shallow: false })
    })
    const intFields = ['maxRows', 'queryHistoryRetentionPeriod']
    intFields.forEach(field => {
        it(`Should parse value as number for ${field} field`, async () => {
            await wrapper.setProps({ value: true }) // open dialog
            const dlg = wrapper.findComponent({ name: 'base-dialog' })
            const inputComponent = dlg.find(`.${field}`)
            await inputChangeMock(inputComponent, '10')
            expect(wrapper.vm.config[field]).to.be.equals(10)
        })
    })

    const boolFields = ['showQueryConfirm', 'showSysSchemas']
    boolFields.forEach(field => {
        it(`Should parse value as boolean for ${field} checkbox`, async () => {
            await wrapper.setProps({ value: true }) // open dialog
            const dlg = wrapper.findComponent({ name: 'base-dialog' })
            const checkboxComponent = dlg.find(`.${field}`).find('input')
            const curVal = wrapper.vm.curCnf[field]
            await checkboxComponent.trigger('click')
            expect(wrapper.vm.config[field]).to.be.equals(!curVal)
        })
    })

    //TODO: add validation tests for maxRows & queryHistoryRetentionPeriod
})

//TODO: add tests for save action
