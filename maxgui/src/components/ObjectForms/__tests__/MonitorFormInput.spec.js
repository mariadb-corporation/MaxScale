/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
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
    getUnMonitoredServersStub,
} from '@tests/unit/utils'
import MonitorFormInput from '@src/components/ObjectForms/MonitorFormInput'
import { MXS_OBJ_TYPES } from '@share/constants'
import { MRDB_MON } from '@src/constants'

const dummyResourceModules = [
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
                    type: 'password',
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
        wrapper = mount({
            shallow: false,
            component: MonitorFormInput,
            propsData: {
                allServers: dummy_all_servers,
                moduleParamsProps: { modules: dummyResourceModules },
            },
        })
    })

    it(`Should pass the following props and have ref to module-parameters`, () => {
        const { moduleName, modules, defModuleId, objType } = wrapper.findComponent({
            name: 'module-parameters',
        }).vm.$props
        expect(moduleName).to.be.equals('module')
        expect(modules).to.be.eqls(wrapper.vm.$props.moduleParamsProps.modules)
        expect(defModuleId).to.equals(MRDB_MON)
        expect(objType).to.equal(MXS_OBJ_TYPES.MONITORS)
        expect(wrapper.vm.$refs.moduleInputs).to.be.not.null
    })

    it(`Should pass the following props and have ref to resource-relationships`, () => {
        const resourceRelationships = wrapper.findComponent({
            name: 'resource-relationships',
        })
        // props
        const { relationshipsType, items, defaultItems } = resourceRelationships.vm.$props
        expect(relationshipsType).to.be.equals('servers')
        expect(items).to.be.deep.equals(wrapper.vm.serversList)
        expect(defaultItems).to.be.deep.equals(wrapper.vm.$props.defaultItems)
        //ref
        expect(wrapper.vm.$refs.serversRelationship).to.be.not.null
    })

    it(`Should get only server that are not monitored`, () => {
        expect(wrapper.vm.serversList).to.be.deep.equals(getUnMonitoredServersStub())
    })

    it(`Should return an object with parameters and relationships objects
      when getValues method get called`, async () => {
        // get a monitor parameter to mockup value changes
        const monitorParameter = dummyResourceModules[0].attributes.parameters[1]
        const parameterCell = wrapper.find(`.cell-${1}-${monitorParameter.name}`)
        const newValue = 'new value'
        await inputChangeMock(parameterCell, newValue)

        // mockup monitor relationships change
        const resourceRelationships = wrapper.findComponent({ name: 'resource-relationships' })
        const serversList = wrapper.vm.serversList // get serversList from computed property
        await itemSelectMock(resourceRelationships, serversList[0])

        const expectedValue = {
            moduleId: dummyResourceModules[0].id,
            parameters: { [monitorParameter.name]: newValue },
            relationships: {
                servers: { data: [getUnMonitoredServersStub()[0]] },
            },
        }
        expect(wrapper.vm.getValues()).to.be.deep.equals(expectedValue)
    })
})
