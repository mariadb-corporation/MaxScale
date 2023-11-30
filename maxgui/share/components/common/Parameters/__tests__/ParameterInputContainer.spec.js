/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import ParameterInputContainer from '@share/components/common/Parameters/ParameterInputContainer'
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
        wrapper = mount({
            shallow: false,
            component: ParameterInputContainer,
            propsData: {
                item: {}, // required props which will be added on each test
                changedParametersArr: [],
                /* changedParametersArr is a required props which
                will be overwritten on test watching on get-changed-params event */
                objType: 'monitors',
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
        const props = {
            item: portParam,
            validate: () => null,
            portValue: portParam.value,
            socketValue: socketParam.value,
            objType: 'servers',
        }
        await wrapper.setProps(props)
        const parameter_input = wrapper.findAllComponents({ name: 'parameter-input' })
        expect(parameter_input.length).to.be.equal(1)
        const { validate, portValue, socketValue, objType } = parameter_input.at(0).vm.$props
        expect(validate).to.be.equal(props.validate)
        expect(portValue).to.be.equal(props.portValue)
        expect(socketValue).to.be.equal(props.socketValue)
        expect(objType).to.be.equal(props.objType)
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
