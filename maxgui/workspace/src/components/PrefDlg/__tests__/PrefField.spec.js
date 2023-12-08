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
import PrefField from '@wsComps/PrefDlg/PrefField'
import { getErrMsgEle, inputChangeMock } from '@tests/unit/utils'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts => mount(lodash.merge({ shallow: true, component: PrefField }, opts))
let wrapper

describe(`PrefField`, () => {
    describe('Row Limit', () => {
        beforeEach(() => {
            wrapper = mountFactory({
                propsData: {
                    value: 10000,
                    field: {
                        name: 'rowLimit',
                        icon: '$vuetify.icons.mxs_statusWarning',
                        iconColor: 'warning',
                        i18nPath: 'mxs.info.rowLimit',
                    },
                    type: 'number',
                },
            })
        })
        it(`Should pass accurate data to row-limit-ctr via attrs`, () => {
            const { height, 'hide-details': hideDetails } = wrapper.findComponent({
                name: 'row-limit-ctr',
            }).vm.$attrs
            expect(height).to.be.equals(36)
            expect(hideDetails).to.be.equals('auto')
        })
        it(`Should handle @change event emitted from row-limit-ctr`, () => {
            const newVal = 123
            wrapper.findComponent({ name: 'row-limit-ctr' }).vm.$emit('change', newVal)
            expect(wrapper.emitted('input')[0][0]).to.be.eql(newVal)
        })
    })
    describe('Form input tests', () => {
        const fieldTestCases = [
            {
                fields: [{ name: 'maxStatements' }, { name: 'queryHistoryRetentionPeriod' }],
                value: 100,
                type: 'number',
            },
            {
                fields: [{ name: 'showQueryConfirm' }, { name: 'showSysSchemas' }],
                value: 100,
                type: 'boolean',
            },
            {
                fields: [
                    { name: 'defConnObjType', enumValues: ['servers', 'services', 'listeners'] },
                ],
                value: 'servers',
                type: 'enum',
            },
        ]
        fieldTestCases.forEach(({ type, fields, value }) => {
            describe(`${type}`, () => {
                fields.forEach(field => {
                    describe(`${field.name}`, () => {
                        beforeEach(() => {
                            wrapper = mountFactory({
                                shallow: false,
                                propsData: { value, field, type },
                            })
                        })
                        switch (type) {
                            case 'number':
                                it(`Parse value as number`, async () => {
                                    const inputComponent = wrapper.findComponent({
                                        name: 'v-text-field',
                                    })
                                    await inputChangeMock(inputComponent, '10')
                                    expect(wrapper.emitted('input')[0][0]).to.be.eql(10)
                                })

                                it(`Show accurate error message when value is 0`, async () => {
                                    const inputComponent = wrapper.findComponent({
                                        name: 'v-text-field',
                                    })
                                    await inputChangeMock(inputComponent, 0)
                                    expect(getErrMsgEle(inputComponent).text()).to.be.equals(
                                        wrapper.vm.$mxs_t('errors.largerThanZero', {
                                            inputName: wrapper.vm.$mxs_t(field.name),
                                        })
                                    )
                                })
                                it(`Show accurate error message when value is empty`, async () => {
                                    const inputComponent = wrapper.findComponent({
                                        name: 'v-text-field',
                                    })
                                    await inputChangeMock(inputComponent, '')
                                    expect(getErrMsgEle(inputComponent).text()).to.be.equals(
                                        wrapper.vm.$mxs_t('errors.requiredInput', {
                                            inputName: wrapper.vm.$mxs_t(field.name),
                                        })
                                    )
                                })
                                break
                            case 'boolean':
                                it(`Render v-checkbox`, () => {
                                    const inputComponent = wrapper.findComponent({
                                        name: 'v-checkbox',
                                    })
                                    expect(inputComponent.exists()).to.be.true
                                })
                                break
                            case 'enum':
                                it(`Render v-select`, () => {
                                    const inputComponent = wrapper.findComponent({
                                        name: 'v-select',
                                    })
                                    expect(inputComponent.exists()).to.be.true
                                })
                                break
                        }
                    })
                })
            })
        })
    })
})
