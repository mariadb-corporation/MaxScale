/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { expect } from 'chai'
import mount from '@tests/unit/setup'
import ParameterInput from '@/components/common/Parameters/ParameterInput'

//component should render one input at a time
function checkRenderOneInput(wrapper) {
    let stdClass = wrapper.findAll('.std')
    expect(stdClass.length).to.be.equal(1)
}

//component component renders accurate input type
async function renderAccurateInputType(wrapper, item, itemType) {
    await wrapper.setProps({
        item: item,
    })
    checkRenderOneInput(wrapper)
    let inputWrapper = wrapper.findAll(`.${itemType}`)
    expect(inputWrapper.length).to.be.equal(1)
}

describe('ParameterInput.vue', () => {
    let wrapper

    beforeEach(() => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: ParameterInput,
            props: {
                item: {},
            },
        })
    })

    it(`bool type: component renders v-select input that
      only allows to select boolean value`, async () => {
        await renderAccurateInputType(
            wrapper,
            {
                default_value: false,
                type: 'bool',
                value: false,
                id: 'proxy_protocol',
            },
            'bool'
        )
    })

    it(`enum_mask type: component renders v-select input that
      allows selecting multiple items`, async () => {
        await renderAccurateInputType(
            wrapper,
            {
                default_value: 'primary_monitor_master',
                enum_values: [
                    'none',
                    'connecting_slave',
                    'connected_slave',
                    'running_slave',
                    'primary_monitor_master',
                ],
                type: 'enum_mask',
                value: 'primary_monitor_master',
                id: 'master_conditions',
            },
            'enum_mask'
        )
    })

    it(`enum type: component renders v-select input that
      allows selecting an item`, async () => {
        await renderAccurateInputType(
            wrapper,
            {
                default_value: 'none',
                enum_values: ['none', 'majority_of_running', 'majority_of_all'],
                type: 'enum',
                value: 'none',
                id: 'cooperative_monitoring_locks',
            },
            'enum'
        )
    })

    it(`count type: component renders v-text-field input that only allows to enter
      number`, async () => {
        await renderAccurateInputType(
            wrapper,
            {
                type: 'count',
                value: 0,
                id: 'extra_port',
            },
            'count'
        )
    })

    it(`int type: component renders v-text-field input that allows to enter
      integers number i.e -1 or 1`, async () => {
        await renderAccurateInputType(
            wrapper,
            { default_value: '-1', type: 'int', value: '-1', id: 'retain_last_statements' },
            'int'
        )
    })

    it(`duration type: component renders v-text-field input that only allows to enter
      number >=0`, async () => {
        await renderAccurateInputType(
            wrapper,
            {
                default_value: '300s',
                type: 'duration',
                unit: 's',
                value: '300s',
                id: 'connection_keepalive',
            },
            'duration'
        )
    })

    it(`size type: component renders v-text-field input that only allows to enter
      number >=0`, async () => {
        await renderAccurateInputType(
            wrapper,
            {
                default_value: '1073741824',
                type: 'size',
                value: '1073741824',
                id: 'transaction_replay_max_size',
            },
            'size'
        )
    })

    it(`password string type: Component renders v-text-field input
    if type is password string`, async () => {
        await renderAccurateInputType(
            wrapper,
            { type: 'password string', value: null, id: 'replication_password' },
            'password-string'
        )
    })

    it(`string type or others: Component renders v-text-field input
      if type is string or others`, async () => {
        await renderAccurateInputType(
            wrapper,
            {
                type: 'string',
                value: null,
                id: 'address',
            },
            'string'
        )
    })
})
