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

/**
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * component should render one input at a time
 */
function checkRenderOneInput(wrapper) {
    let stdClass = wrapper.findAll('.std')
    expect(stdClass.length).to.be.equal(1)
}

/**
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {Object} item a parameter object must contains at least id, value and type attributes
 * @param {String} typeClass type class of parameter object
 * component renders accurate input type
 */
async function renderAccurateInputType(wrapper, item, typeClass) {
    await wrapper.setProps({
        item: item,
    })
    checkRenderOneInput(wrapper)
    let inputWrapper = wrapper.findAll(`.${typeClass}`)
    expect(inputWrapper.length).to.be.equal(1)
}

/**
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} errorMessage a specific error message
 * When required props is true and the input value is invalid, it should renders errorMessage
 */
async function requiredVTextField(wrapper, errorMessage) {
    await wrapper.setProps({
        required: true,
    })

    // mockup null (invalid) input event
    await wrapper.setData({
        targetItem: {
            ...wrapper.vm.$data.targetItem,
            value: null,
        },
    })
    let vTextField = wrapper.findComponent({ name: 'v-text-field' })
    expect(vTextField.vm.valid).to.be.equal(false)
    expect(vTextField.vm.errorBucket.length).to.be.equal(1)
    expect(vTextField.vm.errorBucket[0]).to.be.equal(errorMessage)
}

/**
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} newValueType  js types: number, boolean, string, ...
 * @param {Number} newValue number
 * on-input-change should be fired and returned expected value for count, int, duration or size type
 */
async function testReturnNumberValue(wrapper, newValueType, newValue) {
    wrapper.vm.$on('on-input-change', (newItem, changed) => {
        expect(newItem.value).to.be.a(newValueType)
        expect(newItem.value).to.be.equal(newValue)
        expect(changed).to.be.equal(true)
    })
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

    it(`bool type:
      - component renders v-select input that only allows to select boolean value.
      - component emits on-input-change and returns accurate values `, async () => {
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
        let count = 0
        wrapper.vm.$on('on-input-change', (newItem, changed) => {
            count++
            expect(newItem.value).to.be.a('boolean')
            if (count === 1) {
                expect(newItem.value).to.be.equal(true)
                expect(changed).to.be.equal(true)
            } else if (count === 2) {
                expect(newItem.value).to.be.equal(false)
                expect(changed).to.be.equal(false)
            }
        })

        // mockup onchange event when selecting item
        const vSelect = wrapper.findComponent({ name: 'v-select' })
        // changing from value false to true
        vSelect.vm.selectItem(true)
        // changing back to original value
        vSelect.vm.selectItem(false)

        expect(count).to.be.equal(2)
    })

    it(`enum_mask type:
      - component takes enum_values array and
        renders v-select input that allows selecting multiple items
      - component emits on-input-change and returns enum_mask values as string`, async () => {
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

        let count = 0
        wrapper.vm.$on('on-input-change', (newItem, changed) => {
            count++
            expect(newItem.value).to.be.a('string')
            if (count === 1) {
                expect(newItem.value).to.be.equal('primary_monitor_master,running_slave')
                expect(changed).to.be.equal(true)
            } else if (count === 2) {
                expect(newItem.value).to.be.equal('primary_monitor_master')
                expect(changed).to.be.equal(false)
            } else if (count === 3) {
                expect(newItem.value).to.be.equal('')
                expect(changed).to.be.equal(true)
            }
        })

        // mockup onchange event when selecting item
        const vSelect = wrapper.findComponent({ name: 'v-select' })
        // adding running_slave to value
        vSelect.vm.selectItem('running_slave')
        // removing running_slave from value
        vSelect.vm.selectItem('running_slave')
        // making value empty
        vSelect.vm.selectItem('primary_monitor_master')

        expect(count).to.be.equal(3)
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

        let count = 0
        wrapper.vm.$on('on-input-change', (newItem, changed) => {
            count++
            expect(newItem.value).to.be.a('string')
            if (count === 1) {
                expect(newItem.value).to.be.equal('majority_of_running')
                expect(changed).to.be.equal(true)
            } else if (count === 2) {
                expect(newItem.value).to.be.equal('none')
                expect(changed).to.be.equal(false)
            }
        })

        // mockup onchange event when selecting item
        const vSelect = wrapper.findComponent({ name: 'v-select' })
        // changing from value none to majority_of_running
        vSelect.vm.selectItem('majority_of_running')
        // changing back to original value
        vSelect.vm.selectItem('none')

        expect(count).to.be.equal(2)
    })

    it(`count type:
      - component renders v-text-field input that only allows to enter number
      - component renders error message if 'required' props is true and input value
        is invalid`, async () => {
        await renderAccurateInputType(
            wrapper,
            {
                type: 'count',
                value: 0,
                id: 'extra_port',
            },
            'count'
        )
        await requiredVTextField(wrapper, 'extra_port is required')
        await testReturnNumberValue(wrapper, 'number', 0)
    })

    it(`int type:
      - component renders v-text-field input that allows to enter
        integers number i.e -1 or 1
      - component renders error message if 'required' props is true and input value
        is invalid`, async () => {
        await renderAccurateInputType(
            wrapper,
            { default_value: '-1', type: 'int', value: '-1', id: 'retain_last_statements' },
            'int'
        )
        await requiredVTextField(wrapper, 'retain_last_statements is required')
        await testReturnNumberValue(wrapper, 'number', -1)
    })

    it(`duration type:
      - component renders v-text-field input that only allows to enter number >=0
      - component renders error message if 'required' props is true and input value
        is invalid`, async () => {
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
        await requiredVTextField(wrapper, 'connection_keepalive is required')
        await testReturnNumberValue(wrapper, 'number', 300)
    })

    it(`size type:
      - component renders v-text-field input that only allows to enter number >=0
      - component renders error message if 'required' props is true and input value
        is invalid`, async () => {
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
        await requiredVTextField(wrapper, 'transaction_replay_max_size is required')
        await testReturnNumberValue(wrapper, 'number', 1073741824)
    })

    it(`password string type:
      - Component renders v-text-field input if type is password string
      - component renders error message if 'required' props is true and input value
        is invalid`, async () => {
        await renderAccurateInputType(
            wrapper,
            { type: 'password string', value: '', id: 'replication_password' },
            'password-string'
        )
        await requiredVTextField(wrapper, 'replication_password is required')
    })

    it(`string type or others:
      - Component renders v-text-field input if type is string or others
      - component renders error message if 'required' props is true and input value
        is invalid`, async () => {
        await renderAccurateInputType(
            wrapper,
            {
                type: 'string',
                value: '',
                id: 'test_parameter',
            },
            'string'
        )
        await requiredVTextField(wrapper, 'test_parameter is required')
    })

    it(`address, socket, port parameter of server:
      - Component renders accurate input type
      - component renders error message if 'required' props is true and input value
        is invalid`, async () => {
        // address parameter
        await renderAccurateInputType(
            wrapper,
            {
                type: 'string',
                value: '',
                id: 'address',
            },
            'string'
        )
        await requiredVTextField(wrapper, 'address is required when using port')
        // port parameter
        await renderAccurateInputType(
            wrapper,
            {
                type: 'count',
                value: '',
                id: 'port',
            },
            'count'
        )
        await requiredVTextField(wrapper, 'Either port or socket need to be defined')
        // socket parameter
        await renderAccurateInputType(
            wrapper,
            {
                type: 'string',
                value: '',
                id: 'socket',
            },
            'string'
        )
        await requiredVTextField(wrapper, 'Either port or socket need to be defined')
    })
})
