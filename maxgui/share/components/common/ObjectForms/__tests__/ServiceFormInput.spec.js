/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import {
    itemSelectMock,
    inputChangeMock,
    dummy_all_servers,
    dummy_all_filters,
    getFilterListStub,
} from '@tests/unit/utils'
import ServiceFormInput from '@share/components/common/ObjectForms/ServiceFormInput'

const modulesMockData = [
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
                    type: 'password',
                },
            ],
        },
        id: 'readwritesplit',
    },
]

const routingTargetItemsStub = dummy_all_servers.map(({ id, type }) => ({ id, type }))

describe('ServiceFormInput.vue', () => {
    let wrapper
    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: ServiceFormInput,
            propsData: {
                modules: modulesMockData,
                allFilters: dummy_all_filters,
            },
            data() {
                return {
                    routingTargetItems: routingTargetItemsStub,
                }
            },
        })
    })

    it(`Should pass the following props and have ref to module-parameters`, () => {
        const moduleParameters = wrapper.findComponent({ name: 'module-parameters' })
        const { moduleName, modules } = moduleParameters.vm.$props
        // props
        expect(moduleName).to.be.equals('router')
        expect(modules).to.be.deep.equals(wrapper.vm.$props.modules)
        //ref
        expect(wrapper.vm.$refs.moduleInputs).to.be.not.null
    })

    it(`Should pass the following props to routing-target-select`, () => {
        const routingTargetSelect = wrapper.findComponent({ name: 'routing-target-select' })
        const { value, routingTarget, defaultItems } = routingTargetSelect.vm.$props
        expect(value).to.be.deep.equals(wrapper.vm.$data.routingTargetItems)
        expect(routingTarget).to.be.equals(wrapper.vm.$data.routingTarget)
        expect(defaultItems).to.be.deep.equals(wrapper.vm.$data.defRoutingTargetItems)
    })

    it(`Should pass the following props and have ref to filter resource-relationships`, () => {
        const resourceRelationship = wrapper.findComponent({ name: 'resource-relationships' })
        // props
        const { relationshipsType, items, defaultItems } = resourceRelationship.vm.$props
        expect(relationshipsType).to.be.equals('filters')
        expect(defaultItems).to.be.deep.equals(wrapper.vm.$data.defaultFilterItems)
        expect(items).to.be.deep.equals(wrapper.vm.filtersList)
        //ref
        expect(wrapper.vm.$refs.filtersRelationship).to.be.not.null
    })

    it(`Should compute filtersList from allFilters accurately`, () => {
        expect(wrapper.vm.filtersList).to.be.deep.equals(getFilterListStub())
    })

    it(`Should return an object with parameters and relationships objects
      when getValues method get called`, async () => {
        // mockup select a router module
        const moduleParameters = wrapper.findComponent({ name: 'module-parameters' })
        await itemSelectMock(moduleParameters, modulesMockData[0])

        // get a service parameter to mockup value changes
        const serviceParameter = modulesMockData[0].attributes.parameters[1]
        const parameterCell = wrapper.find(`.cell-${1}-${serviceParameter.name}`)
        const newValue = 'new value'
        await inputChangeMock(parameterCell, newValue)

        // mockup service relationships changes
        const resourceRelationship = wrapper.findComponent({ name: 'resource-relationships' })
        const filtersList = wrapper.vm.filtersList // get filtersList from computed property
        await itemSelectMock(resourceRelationship, filtersList[0])

        const expectedValue = {
            moduleId: modulesMockData[0].id,
            parameters: { [serviceParameter.name]: newValue },
            relationships: {
                servers: { data: routingTargetItemsStub },
                filters: { data: [filtersList[0]] },
            },
        }
        expect(wrapper.vm.getValues()).to.be.deep.equals(expectedValue)
    })
})
