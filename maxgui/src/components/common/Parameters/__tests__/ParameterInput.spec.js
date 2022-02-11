/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import ParameterInput from '@/components/common/Parameters/ParameterInput'
import { itemSelectMock } from '@tests/unit/utils'

let paramHasChild = {
    default_value: false,
    type: 'throttling',
    value: '',
    id: 'log_throttling',
    expanded: false,
}

let boolParam = {
    default_value: false,
    type: 'bool',
    value: false,
    id: 'proxy_protocol',
}

let enumMaskParam = {
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
}

let enumParam = {
    default_value: 'none',
    enum_values: ['none', 'majority_of_running', 'majority_of_all'],
    type: 'enum',
    value: 'none',
    id: 'cooperative_monitoring_locks',
}

let countParam = {
    type: 'count',
    value: 0,
    id: 'extra_port',
}

let intParam = { default_value: '-1', type: 'int', value: '-1', id: 'retain_last_statements' }

let durationParam = {
    default_value: '300s',
    type: 'duration',
    unit: 's',
    value: '300s',
    id: 'connection_keepalive',
}

let sizeParam = {
    default_value: '8192',
    type: 'size',
    value: '8192',
    id: 'writeq_low_water',
}
let passwordParam = { type: 'password string', value: '', id: 'replication_password' }

let stringParam = {
    type: 'string',
    value: '',
    id: 'test_parameter',
}

let addressParam = {
    type: 'string',
    value: '',
    id: 'address',
}

let portParam = {
    type: 'count',
    value: '',
    id: 'port',
}

let socketParam = {
    type: 'string',
    value: '',
    id: 'socket',
}

/**
 * This function tests component renders accurate input type
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {Object} item a parameter object must contains at least id, value and type attributes
 * @param {String} typeClass type class of parameter object
 */
async function renderAccurateInputType(wrapper, item, typeClass) {
    await wrapper.setProps({
        item: item,
    })
    let inputWrapper = wrapper.findAll(`.${typeClass}`)
    expect(inputWrapper.length).to.be.equal(1)
}

/**
 * When mandatory attribute of a parameter is true and the input value is invalid, it should renders errorMessage
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {Object} item a parameter object must contains at least id, value and type attributes
 * @param {String} errorMessage a specific error message
 */
async function requiredVTextField(wrapper, item, errorMessage) {
    await wrapper.setProps({
        item: item,
    })

    // mockup null (invalid) input event
    await wrapper.setData({
        targetItem: {
            ...wrapper.vm.$data.targetItem,
            value: null,
        },
    })
    // Manually trigger input event in v-text-field
    await wrapper
        .findAll('input')
        .at(0)
        .trigger('input')

    let errorMessageDiv = wrapper.find('.v-messages__message').html()
    expect(errorMessageDiv).to.be.include(errorMessage)
}

/**
 * on-input-change should be fired and returned expected value for count, int, duration or size type
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} newValueType  js types: number, boolean, string, ...
 * @param {Number} newValue number
 */
async function testReturnNumberValue(wrapper, item, newValueType, newValue) {
    // original item
    await wrapper.setProps({
        item: item,
    })
    let count = 0
    wrapper.vm.$on('on-input-change', (newItem, changed) => {
        count++
        let chosenSuffix = wrapper.vm.$data.chosenSuffix
        expect(newItem.value).to.be.a(chosenSuffix ? `string` : newValueType)
        expect(newItem.value).to.be.equal(chosenSuffix ? `${newValue}${chosenSuffix}` : newValue)
        expect(changed).to.be.equal(true)
    })

    await wrapper.setData({
        targetItem: {
            ...wrapper.vm.$data.targetItem,
            value: newValue,
        },
    })
    // Manually trigger input event in v-text-field
    await wrapper
        .findAll('input')
        .at(0)
        .trigger('input')
    expect(count).to.be.equal(1)
}

/**
 * This function tests if on-input-change is fired and returned expected value and value type
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {Object} item a parameter object must contains at least id, value and type  attributes
 * @param {String} newSuffix  New unit suffix for size: 'Ki', 'Mi', 'Gi', 'Ti', 'k', 'M', 'G', 'T'
 * or for duration 'ms', 's', 'm', 'h'
 * @param {String} oldSuffix  old unit suffix before updating
 * @param {String} expectedValue Expected value after choosing new suffix
 * @param {String} oldValue The original value before choosing new suffix
 */
async function testSuffixSelection(wrapper, item, newSuffix, oldSuffix, expectedValue, oldValue) {
    await wrapper.setProps({
        item: item,
    })
    let count = 0
    wrapper.vm.$on('on-input-change', (newItem, changed) => {
        count++
        if (count === 1) {
            expect(newItem.value).to.be.a('string')
            expect(newItem.value).to.be.equal(expectedValue)
            expect(changed).to.be.equal(true)
        } else if (count === 2) {
            item.type === 'size'
                ? expect(newItem.value).to.be.a('number')
                : expect(newItem.value).to.be.a('string')

            expect(newItem.value).to.be.equal(oldValue)
            expect(changed).to.be.equal(false)
        }
    })

    // set to new suffix
    await wrapper.setData({
        chosenSuffix: newSuffix,
    })
    // set back to old suffix
    if (item.type === 'size') {
        // for size type, there is no unit, hence undefined is assigned when clear selection
        await wrapper.setData({
            chosenSuffix: undefined,
        })
    } else {
        await wrapper.setData({
            chosenSuffix: oldSuffix,
        })
    }
}

describe('ParameterInput.vue', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: ParameterInput,
            propsData: {
                item: {},
            },
        })
    })

    it(`Component renders empty span if a parameter item has expanded property`, async () => {
        await renderAccurateInputType(wrapper, paramHasChild, 'expandable-param')
    })

    it(`bool type:
      - Should render v-select input that only allows to select boolean value.`, async () => {
        await renderAccurateInputType(wrapper, boolParam, 'bool')
        const vSelect = wrapper.findComponent({ name: 'v-select' })
        expect(vSelect.vm.$props.items).to.be.deep.equal([true, false])
    })
    it(`bool type:
      - Should emit on-input-change and return accurate values`, async () => {
        await renderAccurateInputType(wrapper, boolParam, 'bool')
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
        await itemSelectMock(wrapper, true) // changing from value false to true
        await itemSelectMock(wrapper, false) // changing back to original value

        expect(count).to.be.equal(2)
    })

    it(`enum_mask type:
      - Should render v-select input and take enum_values array as available items `, async () => {
        await renderAccurateInputType(wrapper, enumMaskParam, 'enum_mask')
        const vSelect = wrapper.findComponent({ name: 'v-select' })
        expect(vSelect.vm.$props.items).to.be.deep.equal(enumMaskParam.enum_values)
    })
    it(`enum_mask type:
      - Should emit on-input-change and return accurate values as string
        when selecting multiple items`, async () => {
        await wrapper.setProps({
            item: enumMaskParam,
        })
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
        await itemSelectMock(wrapper, 'running_slave') // adding running_slave to value
        await itemSelectMock(wrapper, 'running_slave') // removing running_slave from value
        await itemSelectMock(wrapper, 'primary_monitor_master') // making value empty

        expect(count).to.be.equal(3)
    })

    it(`enum type:
      - Should render v-select input and take enum_values array as available items `, async () => {
        await renderAccurateInputType(wrapper, enumParam, 'enum')
        const vSelect = wrapper.findComponent({ name: 'v-select' })
        expect(vSelect.vm.$props.items).to.be.deep.equal(enumParam.enum_values)
    })
    it(`enum type:
      - Should emit on-input-change and return accurate values as string
        when selecting an item`, async () => {
        await wrapper.setProps({ item: enumParam })

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
        await itemSelectMock(wrapper, 'majority_of_running') // changing to majority_of_running
        await itemSelectMock(wrapper, 'none') // changing back to original value

        expect(count).to.be.equal(2)
    })

    it(`count type: Component renders v-text-field input that
      only allows to enter number`, async () => {
        await renderAccurateInputType(wrapper, countParam, 'count')
    })
    it(`count type: Component renders error message if
      'mandatory' attribute is true and input value is invalid`, async () => {
        await requiredVTextField(
            wrapper,
            { ...countParam, mandatory: true },
            'extra_port is required'
        )
    })
    it(`count type: Component emits on-input-change event and
      returns accurate value`, async () => {
        await testReturnNumberValue(wrapper, countParam, 'number', 100)
    })

    it(`int type: Component renders v-text-field input that
      allows to enter integers number i.e -1 or 1`, async () => {
        await renderAccurateInputType(wrapper, intParam, 'int')
    })
    it(`int type: Component renders error message if
      'mandatory' attribute is true and input value is invalid`, async () => {
        await requiredVTextField(
            wrapper,
            { ...intParam, mandatory: true },
            'retain_last_statements is required'
        )
    })
    it(`int type: Component emits on-input-change event and
      returns accurate value`, async () => {
        await testReturnNumberValue(wrapper, intParam, 'number', -100)
    })

    it(`duration type: Component renders v-text-field input
      that only allows to enter number >=0`, async () => {
        await renderAccurateInputType(wrapper, durationParam, 'duration')
    })
    it(`duration type: Component renders error message if
      'mandatory' attribute is true and input value is invalid`, async () => {
        await requiredVTextField(
            wrapper,
            { ...durationParam, mandatory: true },
            'connection_keepalive is required'
        )
    })
    it(`duration type: Component emits on-input-change event
      and returns accurate value`, async () => {
        await testReturnNumberValue(wrapper, durationParam, 'string', 350)
    })
    it(`duration type: Component allows to select duration suffix
      and returns accurate value`, async () => {
        await testSuffixSelection(
            wrapper,
            durationParam,
            'ms',
            's',
            '300000ms',
            durationParam.value
        )
    })

    it(`size type: Component renders v-text-field input that
      only allows to enter number >=0`, async () => {
        await renderAccurateInputType(wrapper, sizeParam, 'size')
    })
    it(`size type: Component renders error message if
      'mandatory' attribute is true and input value is invalid`, async () => {
        await requiredVTextField(
            wrapper,
            { ...sizeParam, mandatory: true },
            'writeq_low_water is required'
        )
    })
    it(`size type: Component emits on-input-change event and
      returns accurate value`, async () => {
        await testReturnNumberValue(wrapper, sizeParam, 'number', 16384)
    })
    it(`size type: Component allows to select duration suffix
      and returns accurate value`, async () => {
        // the value will parse to int when passing to v-text-field
        sizeParam.value = parseInt(sizeParam.value)
        await testSuffixSelection(wrapper, sizeParam, 'Ki', '', '8Ki', sizeParam.value)
    })

    it(`password string type: Component renders v-text-field
      input if type is password string`, async () => {
        await renderAccurateInputType(wrapper, passwordParam, 'password-string')
    })
    it(`password string type: Component renders error message if
      'mandatory' attribute is true and input value is invalid`, async () => {
        await requiredVTextField(
            wrapper,
            { ...passwordParam, mandatory: true },
            'replication_password is required'
        )
    })
    it(`password string type: Component allows to toggle masked password`, async () => {
        await wrapper.setProps({ item: passwordParam })
        expect(wrapper.vm.$data.isPwdVisible).to.be.equal(false)
        let toggleMaskPwdBtn = wrapper.findAll('.v-input__append-inner > button')
        let inputs = wrapper.findAll('input')
        expect(inputs.length).to.be.equal(1)
        expect(toggleMaskPwdBtn.length).to.be.equal(1)

        let input = inputs.at(0)
        expect(input.find('[type = "password"]').exists()).to.be.equal(true)

        await toggleMaskPwdBtn.at(0).trigger('click')
        expect(wrapper.vm.$data.isPwdVisible).to.be.equal(true)
        expect(input.find('[type = "text"]').exists()).to.be.equal(true)
    })

    it(`string type or others: Component renders v-text-field
      input if type is string or others`, async () => {
        await renderAccurateInputType(wrapper, stringParam, 'string')
    })
    it(`string type or others: Component renders error
      message if 'mandatory' attribute is true and input value is invalid`, async () => {
        await requiredVTextField(
            wrapper,
            { ...stringParam, mandatory: true },
            'test_parameter is required'
        )
    })

    it(`address parameter: Component renders accurate input type`, async () => {
        await renderAccurateInputType(wrapper, addressParam, 'string')
    })
    it(`address parameter: Component renders error message if
      'mandatory' attribute is true and input value is invalid`, async () => {
        await requiredVTextField(
            wrapper,
            { ...addressParam, mandatory: true },
            'address is required when using port'
        )
    })

    it(`port parameter: Component renders accurate input type`, async () => {
        await renderAccurateInputType(wrapper, portParam, 'count')
    })
    it(`port parameter: Component renders error message if
      'mandatory' attribute is true and input value is invalid`, async () => {
        await requiredVTextField(
            wrapper,
            { ...portParam, mandatory: true },
            'Either port or socket need to be defined'
        )
    })

    it(`socket parameter: Component renders accurate input type`, async () => {
        await renderAccurateInputType(wrapper, socketParam, 'string')
    })
    it(`socket parameter: Component renders error message if
      'mandatory' attribute is true and input value is invalid`, async () => {
        await requiredVTextField(
            wrapper,
            { ...socketParam, mandatory: true },
            'Either port or socket need to be defined'
        )
    })
})
