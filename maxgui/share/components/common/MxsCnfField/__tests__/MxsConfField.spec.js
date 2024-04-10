/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import MxsCnfField from '@share/components/common/MxsCnfField'
import { getErrMsgEle, inputChangeMock } from '@tests/unit/utils'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts => mount(lodash.merge({ shallow: true, component: MxsCnfField }, opts))
let wrapper

describe(`MxsCnfField`, () => {
    const fieldTestCases = [
        {
            fields: [
                { id: 'max_statements', label: 'maxStatements' },
                { id: 'query_history_expired_time', label: 'queryHistoryRetentionPeriod' },
                { id: 'interactive_timeout', label: 'interactive_timeout' },
                { id: 'wait_timeout', label: 'wait_timeout' },
            ],
            value: 100,
            type: 'positiveNumber',
        },
        {
            fields: [
                { id: 'query_confirm_flag', label: 'showQueryConfirm' },
                { id: 'query_show_sys_schemas_flag', label: 'showSysSchemas' },
            ],
            value: 100,
            type: 'boolean',
        },
        {
            fields: [
                {
                    id: 'def_conn_obj_type',
                    label: 'defConnObjType',
                    enumValues: ['servers', 'services', 'listeners'],
                },
            ],
            value: 'servers',
            type: 'enum',
        },
    ]
    fieldTestCases.forEach(({ type, fields, value }) => {
        describe(`${type}`, () => {
            fields.forEach(field => {
                describe(`${field.id}`, () => {
                    beforeEach(() => {
                        wrapper = mountFactory({
                            shallow: false,
                            propsData: { value, field, type },
                        })
                    })
                    switch (type) {
                        case 'positiveNumber':
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
                                        inputName: field.label,
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
                                        inputName: field.label,
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
