/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { expect } from 'chai'
import mount from '@tests/unit/setup'
import {
    itemSelectMock,
    inputChangeMock,
    dummy_all_servers,
    dummy_all_filters,
    getFilterListStub,
} from '@tests/unit/utils'
import ServiceFormInput from '@CreateResource/ServiceFormInput'

const mockupResourceModules = [
    {
        attributes: {
            module_type: 'services',
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
            ],
        },
        id: 'readwritesplit',
    },
]

const getServersListStub = () => dummy_all_servers.map(({ id, type }) => ({ id, type }))

describe('ServiceFormInput.vue', () => {
    let wrapper
    beforeEach(() => {
        localStorage.clear()

        wrapper = mount({
            shallow: false,
            component: ServiceFormInput,
            props: {
                resourceModules: mockupResourceModules,
                allServers: dummy_all_servers,
                allFilters: dummy_all_filters,
            },
        })
    })

    it(`Should pass the following props and have ref to module-parameters`, () => {
        const moduleParameters = wrapper.findComponent({ name: 'module-parameters' })
        const { moduleName, modules } = moduleParameters.vm.$props
        // props
        expect(moduleName).to.be.equals('router')
        expect(modules).to.be.deep.equals(wrapper.vm.$props.resourceModules)
        //ref
        expect(wrapper.vm.$refs.moduleInputs).to.be.not.null
    })

    it(`Should have two resource-relationships components`, () => {
        const resourceRelationships = wrapper.findAllComponents({ name: 'resource-relationships' })
        expect(resourceRelationships.length).to.be.equals(2)
    })

    it(`Should pass the following props and have ref to servers resource-relationships`, () => {
        const resourceRelationships = wrapper
            .findAllComponents({ name: 'resource-relationships' })
            .at(0)
        // props
        const { relationshipsType, items, defaultItems } = resourceRelationships.vm.$props
        expect(relationshipsType).to.be.equals('servers')
        expect(items).to.be.deep.equals(wrapper.vm.serversList)
        expect(defaultItems).to.be.deep.equals(wrapper.vm.$data.defaultServerItems)
        //ref
        expect(wrapper.vm.$refs.serversRelationship).to.be.not.null
    })

    it(`Should pass the following props and have ref to filter resource-relationships`, () => {
        const resourceRelationships = wrapper
            .findAllComponents({ name: 'resource-relationships' })
            .at(1)
        // props
        const { relationshipsType, items, defaultItems } = resourceRelationships.vm.$props
        expect(relationshipsType).to.be.equals('filters')
        expect(defaultItems).to.be.deep.equals(wrapper.vm.$data.defaultFilterItems)
        expect(items).to.be.deep.equals(wrapper.vm.filtersList)
        //ref
        expect(wrapper.vm.$refs.filtersRelationship).to.be.not.null
    })

    it(`Should compute serversList from allServers accurately`, async () => {
        expect(wrapper.vm.serversList).to.be.deep.equals(getServersListStub())
    })

    it(`Should compute filtersList from allFilters accurately`, async () => {
        expect(wrapper.vm.filtersList).to.be.deep.equals(getFilterListStub)
    })

    it(`Should return an object with parameters and relationships objects
      when getValues method get called`, async () => {
        // mockup select a router module
        const moduleParameters = wrapper.findComponent({ name: 'module-parameters' })
        await itemSelectMock(moduleParameters, mockupResourceModules[0])

        // get a service parameter to mockup value changes
        const serviceParameter = mockupResourceModules[0].attributes.parameters[1]
        const parameterCell = wrapper.find(`.cell-${1}-${serviceParameter.name}`)
        const newValue = 'new value'
        await inputChangeMock(parameterCell, newValue)

        // mockup service relationships changes
        const resourceRelationships = wrapper.findAllComponents({ name: 'resource-relationships' })
        const serversList = wrapper.vm.serversList // get serversList from computed property
        const filtersList = wrapper.vm.filtersList // get filtersList from computed property
        await itemSelectMock(resourceRelationships.at(0), serversList[0])
        await itemSelectMock(resourceRelationships.at(1), filtersList[0])

        const expectedValue = {
            moduleId: mockupResourceModules[0].id,
            parameters: { [serviceParameter.name]: newValue },
            relationships: {
                servers: { data: [getServersListStub()[0]] },
                filters: { data: [getFilterListStub[0]] },
            },
        }
        expect(wrapper.vm.getValues()).to.be.deep.equals(expectedValue)
    })
})
