/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { expect } from 'chai'
import mount from '@tests/unit/setup'
import ParameterInputContainer from '@/components/common/Parameters/ParameterInputContainer'
import { itemSelectMock, inputChangeMock } from '@tests/unit/utils'

let stringParam = {
    type: 'string',
    value: '',
    id: 'test_parameter',
}

let portParam = {
    type: 'count',
    value: 4001,
    id: 'port',
}

let socketParam = {
    type: 'string',
    value: '',
    id: 'socket',
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

describe('ParameterInputContainer.vue', () => {
    let wrapper

    beforeEach(() => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: ParameterInputContainer,
            props: {
                item: {}, // required props which will be added on each test
                changedParametersArr: [],
                /* changedParametersArr is a required props which
                will be overwritten on test watching on get-changed-params event */
            },
        })
    })

    it(`Component emits handle-change event and returns accurate values
       when there is changes in user input`, async () => {
        // original state
        await wrapper.setProps({
            item: stringParam,
        })
        let count = 0
        wrapper.vm.$on('handle-change', newItem => {
            count++
            expect(newItem.value).to.be.equal('new value')
        })
        // mocking input changes
        await inputChangeMock(wrapper, 'new value')
        expect(count).to.be.equal(1)
    })

    it(`Component passes dependency props to a parameter-input component to handle
      address, port, socket required rule when updating or creating a server`, async () => {
        // when usePortOrSocket is true, the below props should be passed to parameter-input
        let dependencyProps = {
            parentForm: {}, // form ref from parent component
            portValue: portParam.value,
            socketValue: socketParam.value,
            isListener: false,
        }
        await wrapper.setProps({
            item: portParam,
            // below are optional props which are required when updating or creating a server
            usePortOrSocket: true,
            ...dependencyProps,
        })
        const parameter_input = wrapper.findAllComponents({ name: 'parameter-input' })
        expect(parameter_input.length).to.be.equal(1)
        let parameter_input_props = parameter_input.at(0).vm.$props
        expect(parameter_input_props.parentForm).to.be.equal(dependencyProps.parentForm)
        expect(parameter_input_props.portValue).to.be.equal(dependencyProps.portValue)
        expect(parameter_input_props.socketValue).to.be.equal(dependencyProps.socketValue)
        expect(parameter_input_props.isListener).to.be.equal(dependencyProps.isListener)
    })

    it(`Deep changes (Adding new item) of an enum mask parameter should emit
      get-changed-params event and return accurate values`, async () => {
        let changedParametersArr = []
        // original state
        await wrapper.setProps({
            item: enumMaskParam,
            changedParametersArr: changedParametersArr,
        })

        wrapper.vm.$on('get-changed-params', changedParams => {
            changedParametersArr = changedParams
            expect(changedParams.length).to.be.equal(1)
            expect(changedParams[0].value).to.be.a('string')
            expect(changedParams[0].value).to.be.equal('primary_monitor_master,running_slave')
        })

        // mockup selecting item on an enum mask parameter
        await itemSelectMock(wrapper, 'running_slave') // adding running_slave to value

        expect(changedParametersArr.length).to.be.equal(1)
        expect(changedParametersArr[0].value).to.be.equal('primary_monitor_master,running_slave')
    })

    it(`Deep changes (Remove selected item) of an enum mask parameter should emit
      get-changed-params event and return accurate values`, async () => {
        let changedParametersArr = []
        // original state
        await wrapper.setProps({
            item: enumMaskParam,
            changedParametersArr: changedParametersArr,
        })

        wrapper.vm.$on('get-changed-params', changedParams => {
            changedParametersArr = changedParams
            expect(changedParams.length).to.be.equal(1)
            expect(changedParams[0].value).to.be.a('string')
            expect(changedParams[0].value).to.be.equal('')
        })

        // mockup selecting item on an enum mask parameter
        await itemSelectMock(wrapper, 'primary_monitor_master')

        expect(changedParametersArr.length).to.be.equal(1)
        expect(changedParametersArr[0].value).to.be.equal('')
    })
})
