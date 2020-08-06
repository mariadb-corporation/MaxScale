/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { expect } from 'chai'
import mount from '@tests/unit/setup'
import {
    mockupSelection,
    mockupInputChange,
    mockupAllServers,
    mockupServersList,
} from '@tests/unit/mockup'
import MonitorFormInput from '@CreateResource/MonitorFormInput'

const mockupResourceModules = [
    {
        attributes: {
            module_type: 'Monitor',
            parameters: [
                {
                    mandatory: true,
                    name: 'user',
                    type: 'string',
                },
                {
                    mandatory: true,
                    name: 'password',
                    type: 'password string',
                },
                {
                    mandatory: false,
                    name: 'detect_stale_slave',
                    type: 'bool',
                },
            ],
        },
        id: 'mariadbmon',
    },
]

describe('MonitorFormInput.vue', () => {
    let wrapper
    beforeEach(() => {
        localStorage.clear()

        wrapper = mount({
            shallow: false,
            component: MonitorFormInput,
            props: {
                resourceModules: mockupResourceModules,
                allServers: mockupAllServers,
            },
        })
    })

    it(`Should pass the following props and have ref to module-parameters`, () => {
        const moduleParameters = wrapper.findComponent({ name: 'module-parameters' })
        const { moduleName, modules } = moduleParameters.vm.$props
        // props
        expect(moduleName).to.be.equals('module')
        expect(modules).to.be.deep.equals(wrapper.vm.$props.resourceModules)
        //ref
        expect(wrapper.vm.$refs.moduleInputs).to.be.not.null
    })

    it(`Should pass the following props and have ref to resource-relationships`, () => {
        const resourceRelationships = wrapper.findComponent({ name: 'resource-relationships' })
        // props
        const { relationshipsType, items } = resourceRelationships.vm.$props
        expect(relationshipsType).to.be.equals('servers')
        expect(items).to.be.deep.equals(wrapper.vm.serversList)
        //ref
        expect(wrapper.vm.$refs.serversRelationship).to.be.not.null
    })

    it(`Should compute serversList from allServers accurately`, async () => {
        expect(wrapper.vm.serversList).to.be.deep.equals(mockupServersList)
    })

    it(`Should return an object with parameters and relationships objects
      when getValues method get called`, async () => {
        // mockup select a monitor module
        const moduleParameters = wrapper.findComponent({ name: 'module-parameters' })
        await mockupSelection(moduleParameters, mockupResourceModules[0])

        // get a monitor parameter to mockup value changes
        const monitorParameter = mockupResourceModules[0].attributes.parameters[1]
        const parameterCell = wrapper.find(`.${monitorParameter.name}-cell-${1}`)
        const newValue = 'new value'
        await mockupInputChange(parameterCell, newValue)

        // mockup monitor relationships change
        const resourceRelationships = wrapper.findComponent({ name: 'resource-relationships' })
        const serversList = wrapper.vm.serversList // get serversList from computed property
        await mockupSelection(resourceRelationships, serversList[0])

        const expectedValue = {
            moduleId: mockupResourceModules[0].id,
            parameters: { [monitorParameter.name]: newValue },
            relationships: {
                servers: { data: [mockupServersList[0]] },
            },
        }
        expect(wrapper.vm.getValues()).to.be.deep.equals(expectedValue)
    })
})
