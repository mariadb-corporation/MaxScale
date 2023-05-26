/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import QueryCnfDlg from '../QueryCnfDlg'
import { getErrMsgEle, inputChangeMock } from '@tests/unit/utils'
import { addDaysToNow } from '@queryEditorSrc/utils/helpers'

const defCnf = {
    query_row_limit: 10000,
    query_confirm_flag: 1,
    query_history_expired_time: addDaysToNow(30),
    query_show_sys_schemas_flag: 1,
}
const mountFactory = opts =>
    mount({
        shallow: true,
        component: QueryCnfDlg,
        propsData: {
            value: true, // open dialog
            cnf: defCnf,
        },
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

describe(`QueryCnfDlg - child component's data communication tests `, () => {
    it(`Should pass accurate data to mxs-dlg via props`, () => {
        let wrapper = mountFactory()
        const { value, title, onSave, lazyValidation, hasChanged } = wrapper.findComponent({
            name: 'mxs-dlg',
        }).vm.$props
        expect(value).to.be.equals(wrapper.vm.isOpened)
        expect(title).to.be.equals(wrapper.vm.$mxs_t('queryConfig'))
        expect(onSave).to.be.equals(wrapper.vm.onSave)
        expect(lazyValidation).to.be.false
        expect(hasChanged).to.be.equals(wrapper.vm.hasChanged)
    })
    it(`Should pass accurate data to row-limit-ctr via attrs`, () => {
        let wrapper = mountFactory()
        const input = wrapper.findComponent({ name: 'row-limit-ctr' })
        const { height, 'hide-details': hideDetails } = input.vm.$attrs
        expect(input.vm.$vnode.key).to.be.equals(wrapper.vm.isOpened)
        expect(height).to.be.equals(36)
        expect(hideDetails).to.be.equals('auto')
    })
    it(`Should handle @change event emitted from row-limit-ctr`, () => {
        let wrapper = mountFactory()
        const newVal = wrapper.vm.$data.config.rowLimit + 123
        wrapper.findComponent({ name: 'row-limit-ctr' }).vm.$emit('change', newVal)
        expect(wrapper.vm.$data.config.rowLimit).to.be.equals(newVal)
    })
})

describe(`QueryCnfDlg - tests after dialog is opened `, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory()
    })
    it(`Should deep copy defCnf to config after dialog is opened `, async () => {
        expect(wrapper.vm.config).to.be.deep.equals(wrapper.vm.defCnf)
    })
    it(`Should return accurate value for hasChanged`, async () => {
        await wrapper.setProps({ value: true }) // open dialog
        expect(wrapper.vm.hasChanged).to.be.false // no changes to form yet
        await mockChangingConfig({ wrapper, key: 'rowLimit', value: 1 })
        expect(wrapper.vm.hasChanged).to.be.true
    })
})

describe(`QueryCnfDlg - form input tests`, () => {
    let wrapper
    beforeEach(() => {
        //open dialog when component is mounted by assigning true to isOpened
        wrapper = mountFactory({ shallow: false })
    })
    const intFields = ['queryHistoryRetentionPeriod']
    intFields.forEach(field => {
        it(`Should parse value as number for ${field} field`, async () => {
            const inputComponent = wrapper.findComponent({ name: 'mxs-dlg' }).find(`.${field}`)
            await inputChangeMock(inputComponent, '10')
            expect(wrapper.vm.config[field]).to.be.equals(10)
        })
    })

    const boolFields = ['showQueryConfirm', 'showSysSchemas']
    boolFields.forEach(field => {
        it(`Should parse value as boolean for ${field} checkbox`, async () => {
            const checkboxComponent = wrapper
                .findComponent({ name: 'mxs-dlg' })
                .find(`.${field}`)
                .find('input')
            const curVal = wrapper.vm.defCnf[field]
            await checkboxComponent.trigger('click')
            expect(wrapper.vm.config[field]).to.be.equals(!curVal)
        })
    })

    intFields.forEach(field => {
        it(`Should show accurate error message when ${field} value is 0`, async () => {
            const inputComponent = wrapper.findComponent({ name: 'mxs-dlg' }).find(`.${field}`)
            await inputChangeMock(inputComponent, 0)
            expect(getErrMsgEle(inputComponent).text()).to.be.equals(
                wrapper.vm.$mxs_t('errors.largerThanZero', {
                    inputName: wrapper.vm.$mxs_t(field),
                })
            )
        })
        it(`Should show accurate error message when ${field} value is empty`, async () => {
            const inputComponent = wrapper.findComponent({ name: 'mxs-dlg' }).find(`.${field}`)
            await inputChangeMock(inputComponent, '')
            expect(getErrMsgEle(inputComponent).text()).to.be.equals(
                wrapper.vm.$mxs_t('errors.requiredInput', { inputName: wrapper.vm.$mxs_t(field) })
            )
        })
    })

    it(`Should emit 'confirm-save' event with accurate args`, async () => {
        wrapper = mountFactory()
        await wrapper.vm.onSave()
        expect(wrapper.emitted()).to.have.property('confirm-save')
        const arg = wrapper.emitted()['confirm-save'][0][0]
        const keys = [
            'query_row_limit',
            'query_confirm_flag',
            'query_history_expired_time',
            'query_show_sys_schemas_flag',
        ]
        keys.forEach(key => {
            expect(arg).to.have.property(key)
            expect(arg[key]).to.be.a('number')
        })
    })
})
