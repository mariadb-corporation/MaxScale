/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import QueryCnfDlgCtr from '@wsComps/QueryCnfDlgCtr.vue'
import { getErrMsgEle, inputChangeMock } from '@tests/unit/utils'
import { addDaysToNow } from '@wsSrc/utils/helpers'

const mountFactory = opts =>
    mount({
        shallow: true,
        component: QueryCnfDlgCtr,
        attrs: {
            value: true, // open dialog
        },
        computed: {
            query_row_limit: () => 10000,
            query_confirm_flag: () => 1,
            query_history_expired_time: () => addDaysToNow(30),
            query_show_sys_schemas_flag: () => 1,
            tab_moves_focus: () => false,
            max_statements: () => 1000,
            identifier_auto_completion: () => true,
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

describe(`QueryCnfDlgCtr`, () => {
    describe(`Child component's data communication tests`, () => {
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
            const newVal = 123
            wrapper.findComponent({ name: 'row-limit-ctr' }).vm.$emit('change', newVal)
            expect(wrapper.vm.$data.config.rowLimit).to.be.equals(newVal)
        })
    })

    describe(`Tests after dialog is opened`, () => {
        let wrapper
        beforeEach(() => {
            wrapper = mountFactory()
        })
        it(`Should deep copy persistedCnf to config after dialog is opened `, async () => {
            expect(wrapper.vm.config).to.be.deep.equals(wrapper.vm.persistedCnf)
        })
        it(`Should return accurate value for hasChanged`, async () => {
            await wrapper.setProps({ value: true }) // open dialog
            expect(wrapper.vm.hasChanged).to.be.false // no changes to form yet
            await mockChangingConfig({ wrapper, key: 'rowLimit', value: 1 })
            expect(wrapper.vm.hasChanged).to.be.true
        })
    })

    describe(`Form input tests`, () => {
        let wrapper
        beforeEach(() => {
            wrapper = mountFactory({ shallow: false })
        })

        const flagFields = ['showQueryConfirm', 'showSysSchemas']
        flagFields.forEach(field => {
            it(`Should parse flag value to boolean for ${field}`, () => {
                expect(wrapper.vm.persistedCnf[field]).to.be.a('boolean')
            })
        })

        const numericFields = ['maxStatements', 'queryHistoryRetentionPeriod']
        numericFields.forEach(field => {
            it(`Should parse value as number for ${field} field`, async () => {
                const inputComponent = wrapper.find(`.${field}`)
                await inputChangeMock(inputComponent, '10')
                expect(wrapper.vm.config[field]).to.be.equals(10)
            })

            it(`Should show accurate error message when ${field} value is 0`, async () => {
                const inputComponent = wrapper.find(`.${field}`)
                await inputChangeMock(inputComponent, 0)
                expect(getErrMsgEle(inputComponent).text()).to.be.equals(
                    wrapper.vm.$mxs_t('errors.largerThanZero', {
                        inputName: wrapper.vm.$mxs_t(field),
                    })
                )
            })
            it(`Should show accurate error message when ${field} value is empty`, async () => {
                const inputComponent = wrapper.find(`.${field}`)
                await inputChangeMock(inputComponent, '')
                expect(getErrMsgEle(inputComponent).text()).to.be.equals(
                    wrapper.vm.$mxs_t('errors.requiredInput', {
                        inputName: wrapper.vm.$mxs_t(field),
                    })
                )
            })
        })

        const boolFields = [
            'showQueryConfirm',
            'showSysSchemas',
            'tabMovesFocus',
            'identifierAutoCompletion',
        ]

        boolFields.forEach(field => {
            it(`${field} should has boolean value`, async () => {
                const inputComponent = wrapper.find(`.${field}`)
                const inputValue = inputComponent.vm.$props.inputValue
                expect(inputValue).to.be.a('boolean')
                expect(inputValue).to.equals(wrapper.vm.config[field])
            })
        })
    })
})
