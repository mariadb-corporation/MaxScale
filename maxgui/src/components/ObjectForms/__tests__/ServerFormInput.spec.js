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
import {
    itemSelectMock,
    inputChangeMock,
    dummy_all_services,
    getServiceListStub,
    dummy_all_monitors,
    getMonitorListStub,
} from '@tests/unit/utils'
import ServerFormInput from '@src/components/ObjectForms/ServerFormInput'
import { MXS_OBJ_TYPES } from '@share/constants'

const modulesMockData = [
    {
        attributes: {
            module_type: 'servers',
            parameters: [
                {
                    description: 'Server address',
                    mandatory: false,
                    modifiable: true,
                    name: 'address',
                    type: 'string',
                },
                {
                    default_value: 3306,
                    description: 'Server port',
                    mandatory: false,
                    modifiable: true,
                    name: 'port',
                    type: 'count',
                },
                {
                    description: 'Server UNIX socket',
                    mandatory: false,
                    modifiable: true,
                    name: 'socket',
                    type: 'string',
                },
            ],
        },
        id: 'servers',
    },
]

describe('ServerFormInput.vue', () => {
    let wrapper
    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: ServerFormInput,
            propsData: {
                allServices: dummy_all_services,
                allMonitors: dummy_all_monitors,
                moduleParamsProps: { modules: modulesMockData, validate: () => null },
            },
        })
    })

    it(`Should pass the following props and have ref to module-parameters`, () => {
        const { defModuleId, modules, validate, objType } = wrapper.findComponent({
            name: 'module-parameters',
        }).vm.$props
        expect(defModuleId).to.equals(MXS_OBJ_TYPES.SERVERS)
        expect(modules).to.be.eqls(wrapper.vm.$props.moduleParamsProps.modules)
        expect(validate).to.be.deep.equals(wrapper.vm.$props.moduleParamsProps.validate)
        expect(objType).to.equal(MXS_OBJ_TYPES.SERVERS)
        expect(wrapper.vm.$refs.moduleInputs).to.be.not.null
    })

    it(`Should pass the following props and have ref to service resource-relationships`, () => {
        const resourceRelationships = wrapper
            .findAllComponents({ name: 'resource-relationships' })
            .at(0)
        const { relationshipsType, items, defaultItems } = resourceRelationships.vm.$props
        expect(relationshipsType).to.be.equals('services')
        expect(defaultItems).to.be.deep.equals(wrapper.vm.$data.defaultServiceItems)
        expect(items).to.be.deep.equals(wrapper.vm.servicesList)
        expect(wrapper.vm.$refs.servicesRelationship).to.be.not.null
    })

    it(`Should pass the following props and have ref to monitor resource-relationships`, () => {
        const resourceRelationships = wrapper
            .findAllComponents({ name: 'resource-relationships' })
            .at(1)
        // props
        const {
            relationshipsType,
            items,
            multiple,
            defaultItems,
            clearable,
        } = resourceRelationships.vm.$props

        expect(relationshipsType).to.be.equals('monitors')
        expect(clearable).to.be.true
        expect(items).to.be.deep.equals(wrapper.vm.monitorsList)
        expect(defaultItems).to.be.deep.equals(wrapper.vm.$data.defMonitor)
        expect(multiple).to.be.false
        expect(wrapper.vm.$refs.monitorsRelationship).to.be.not.null
    })

    it(`Should compute servicesList from allServices accurately`, () => {
        expect(wrapper.vm.servicesList).to.be.deep.equals(getServiceListStub)
    })

    it(`Should compute monitorsList from allMonitors accurately`, () => {
        expect(wrapper.vm.monitorsList).to.be.deep.equals(getMonitorListStub)
    })

    const withRelationshipTestCases = [{ withRelationship: true }, { withRelationship: false }]

    withRelationshipTestCases.forEach(({ withRelationship }) => {
        it(`Should ${
            withRelationship ? '' : 'not'
        } render resource-relationships components`, async () => {
            await wrapper.setProps({ withRelationship })
            expect(
                wrapper.findAllComponents({
                    name: 'resource-relationships',
                }).length
            ).to.equal(withRelationship ? 2 : 0)
        })

        it(`getValues method should return expected values when
        withRelationship props is ${withRelationship}`, async () => {
            await wrapper.setProps({ withRelationship })
            // get a server parameter to mockup value changes
            const serverParameter = modulesMockData[0].attributes.parameters[1]
            const parameterCell = wrapper.find(`.cell-${1}-${serverParameter.name}`)
            const newValue = 'new value'
            await inputChangeMock(parameterCell, newValue)

            if (withRelationship) {
                // mockup server relationships changes
                const resourceRelationships = wrapper.findAllComponents({
                    name: 'resource-relationships',
                })
                const { servicesList, monitorsList } = wrapper.vm
                await itemSelectMock(resourceRelationships.at(0), servicesList[0])
                await itemSelectMock(resourceRelationships.at(1), monitorsList[0])
            }
            const parameters = { [serverParameter.name]: newValue }

            const values = wrapper.vm.getValues()
            if (withRelationship)
                expect(values).to.eqls({
                    parameters,
                    relationships: {
                        services: { data: [getServiceListStub[0]] },
                        monitors: { data: [getMonitorListStub[0]] },
                    },
                })
            else expect(values).to.eqls({ parameters })
        })
    })
})
