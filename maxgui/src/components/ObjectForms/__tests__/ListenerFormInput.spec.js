/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import {
    itemSelectMock,
    inputChangeMock,
    dummy_all_services,
    getServiceListStub,
} from '@tests/unit/utils'
import ListenerFormInput from '@src/components/ObjectForms/ListenerFormInput'
import { MXS_OBJ_TYPES } from '@share/constants'
import { MRDB_PROTOCOL } from '@src/constants'

const modulesMockData = [
    {
        attributes: {
            module_type: 'Protocol',
            parameters: [
                {
                    mandatory: true,
                    name: 'protocol',
                    type: 'string',
                    default_value: 'mariadbclient',
                    disabled: true,
                },
                { mandatory: false, name: 'socket', type: 'string' },
            ],
        },
        id: 'mariadbclient',
    },
]

describe('ListenerFormInput.vue', () => {
    let wrapper
    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: ListenerFormInput,
            propsData: {
                allServices: dummy_all_services,
                moduleParamsProps: { modules: modulesMockData, validate: () => null },
            },
        })
    })

    it(`Should pass the following props and have ref to module-parameters`, () => {
        const { moduleName, modules, validate, defModuleId, objType } = wrapper.findComponent({
            name: 'module-parameters',
        }).vm.$props
        expect(moduleName).to.be.equals('protocol')
        expect(modules).to.be.eqls(wrapper.vm.$props.moduleParamsProps.modules)
        expect(validate).to.be.a('function')
        expect(defModuleId).to.equal(MRDB_PROTOCOL)
        expect(objType).to.equal(MXS_OBJ_TYPES.LISTENERS)
        expect(wrapper.vm.$refs.moduleInputs).to.be.not.null
    })

    it(`Should pass the following props and have ref to resource-relationships`, () => {
        const resourceRelationships = wrapper.findComponent({ name: 'resource-relationships' })
        // props
        const {
            relationshipsType,
            items,
            multiple,
            required,
            defaultItems,
        } = resourceRelationships.vm.$props
        expect(relationshipsType).to.be.equals('services')
        expect(items).to.be.deep.equals(wrapper.vm.serviceList)
        expect(defaultItems).to.be.deep.equals(wrapper.vm.$props.defaultItems)
        expect(multiple).to.be.false
        expect(required).to.be.true
        //ref
        expect(wrapper.vm.$refs.servicesRelationship).to.be.not.null
    })

    it(`Should compute serviceList from allServices accurately`, () => {
        expect(wrapper.vm.serviceList).to.be.deep.equals(getServiceListStub)
    })

    it(`Should return an object with parameters and relationships objects
      when getValues method get called`, async () => {
        // mockup select a listener module
        const moduleParameters = wrapper.findComponent({ name: 'module-parameters' })
        await itemSelectMock(moduleParameters, modulesMockData[0])

        // get a listener parameter to mockup value changes
        const listenParameter = modulesMockData[0].attributes.parameters[1]
        const parameterCell = wrapper.find(`.cell-${1}-${listenParameter.name}`)
        const newValue = 'new value'
        await inputChangeMock(parameterCell, newValue)

        // mockup listener relationships change
        const resourceRelationships = wrapper.findComponent({ name: 'resource-relationships' })
        const serviceList = wrapper.vm.serviceList // get serviceList from computed property
        await itemSelectMock(resourceRelationships, serviceList[0])

        const expectedValue = {
            parameters: { [listenParameter.name]: newValue, protocol: modulesMockData[0].id },
            relationships: {
                services: { data: [getServiceListStub[0]] },
            },
        }
        expect(wrapper.vm.getValues()).to.be.deep.equals(expectedValue)
    })
})
