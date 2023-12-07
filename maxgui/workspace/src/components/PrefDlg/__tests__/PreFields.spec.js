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
import PrefFields from '@wsComps/PrefDlg/PrefFields'
import { getErrMsgEle, inputChangeMock } from '@tests/unit/utils'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            { shallow: true, component: PrefFields, propsData: { value: {}, data: {} } },
            opts
        )
    )
let wrapper

describe(`PrefFields`, () => {
    describe('Row Limit', () => {
        beforeEach(() => {
            wrapper = mountFactory({
                propsData: {
                    value: { rowLimit: 10000 },
                    data: {
                        numericFields: [
                            {
                                name: 'rowLimit',
                                icon: '$vuetify.icons.mxs_statusWarning',
                                iconColor: 'warning',
                                i18nPath: 'mxs.info.rowLimit',
                            },
                        ],
                    },
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
            expect(wrapper.vm.preferences.rowLimit).to.be.equals(newVal)
        })
    })
    describe(`Form input tests`, () => {
        beforeEach(() => {
            wrapper = mountFactory({
                shallow: false,
                propsData: {
                    value: { maxStatements: 1000, queryHistoryRetentionPeriod: 30 },
                    data: {
                        numericFields: [
                            {
                                name: 'maxStatements',
                                icon: '$vuetify.icons.mxs_statusWarning',
                                iconColor: 'warning',
                                i18nPath: 'mxs.info.maxStatements',
                            },
                            {
                                name: 'queryHistoryRetentionPeriod',
                                suffix: 'days',
                            },
                        ],
                    },
                },
            })
        })
        const numericFields = ['maxStatements', 'queryHistoryRetentionPeriod']
        numericFields.forEach(field => {
            it(`Should parse value as number for ${field} field`, async () => {
                const inputComponent = wrapper.find(`.${field}`)
                await inputChangeMock(inputComponent, '10')
                expect(wrapper.vm.preferences[field]).to.be.equals(10)
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
    })
})
