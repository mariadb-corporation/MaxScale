/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import { itemSelectMock, inputChangeMock } from '@tests/unit/utils'
import ModuleParameters from '@src/components/ObjectForms/ModuleParameters'

const mockupModules = [
    {
        attributes: {
            module_type: 'Router',
            parameters: [
                { mandatory: true, name: 'password' },
                { mandatory: false, name: 'delayed_retry' },
            ],
        },
        id: 'readwritesplit',
    },
    {
        attributes: {
            module_type: 'Router',
            parameters: [
                { mandatory: true, name: 'password' },
                { mandatory: false, name: 'log_auth_warnings' },
            ],
        },
        id: 'readconnroute',
    },
]
const moduleName = 'router'

describe('ModuleParameters.vue', () => {
    let wrapper
    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: ModuleParameters,
            propsData: {
                modules: mockupModules,
                moduleName,
                showAdvanceToggle: false,
                objType: 'services',
            },
        })
    })

    it(`Should render module name as input label accurately`, () => {
        expect(wrapper.find('[data-test="label"]').text()).to.equal(moduleName)
    })

    it(`Should render error message if selectedModule is empty`, async () => {
        await itemSelectMock(wrapper, '')
        let errorMessage = wrapper.find('.v-messages__message').text()
        expect(errorMessage).to.be.equals('router is required')
    })

    it(`Should assign object to selectedModule when a module is selected`, async () => {
        await itemSelectMock(wrapper, mockupModules[0])
        expect(wrapper.vm.$data.selectedModule)
            .to.be.an('object')
            .and.to.be.equals(mockupModules[0])
    })

    it(`Should return parameters from selectedModule`, async () => {
        await itemSelectMock(wrapper, mockupModules[1])
        const moduleParameters = wrapper.vm.moduleParameters
        expect(moduleParameters).to.be.deep.equals(mockupModules[1].attributes.parameters)
    })

    it(`Should pass the following value as props and ref to parameters-collapse`, async () => {
        await itemSelectMock(wrapper, mockupModules[0])
        const { parameters, validate, search, objType } = wrapper.findComponent({
            name: 'parameters-collapse',
        }).vm.$props
        expect(parameters).to.be.deep.equals(wrapper.vm.moduleParameters)
        expect(validate).to.be.deep.equals(wrapper.vm.$props.validate)
        expect(search).to.equal(wrapper.vm.$props.search)
        expect(objType).to.equal(wrapper.vm.$props.objType)
        expect(wrapper.vm.$refs.parametersTable).to.be.not.null
    })

    it(`Should return module inputs object including valid moduleId and
      empty parameters object`, async () => {
        await itemSelectMock(wrapper, mockupModules[1])
        expect(wrapper.vm.getModuleInputValues()).to.be.deep.equals({
            moduleId: mockupModules[1].id,
            parameters: {},
        })
    })

    it(`Should return module inputs object including valid moduleId and parameters`, async () => {
        await itemSelectMock(wrapper, mockupModules[1])
        const testParam = mockupModules[1].attributes.parameters[1]

        const moduleParamTd = wrapper.find(`.cell-${1}-${testParam.name}`)
        const newValue = 'new value'
        await inputChangeMock(moduleParamTd, 'new value')

        expect(wrapper.vm.getModuleInputValues()).to.be.deep.equals({
            moduleId: mockupModules[1].id,
            parameters: { [`${testParam.name}`]: newValue },
        })
    })

    it(`Should use defModuleId as the default module`, async () => {
        await wrapper.setProps({ defModuleId: mockupModules.at(-1).id })
        expect(wrapper.vm.$data.selectedModule).to.eql(mockupModules.at(-1))
    })

    it(`Should hide module options dropdown`, async () => {
        await wrapper.setProps({ hideModuleOpts: true })
        expect(wrapper.find('[data-test="label"]').exists()).to.be.false
        expect(wrapper.find('#module-select').exists()).to.be.false
    })

    it(`Should show only mandatory parameters`, async () => {
        const selectedModule = mockupModules[0]
        await wrapper.setProps({ showAdvanceToggle: true, defModuleId: selectedModule.id })
        expect(wrapper.vm.moduleParameters).to.eqls(
            selectedModule.attributes.parameters.filter(p => p.mandatory)
        )
    })

    it(`specialParams should return expected value`, () => {
        expect(wrapper.vm.specialParams).to.eqls(['address', 'port', 'socket'])
    })

    it(`Should show special parameters regardless of mandatory value`, async () => {
        const mockServerModule = {
            attributes: {
                parameters: [
                    { mandatory: false, name: 'address' },
                    { mandatory: false, name: 'port' },
                    { mandatory: false, name: 'socket' },
                    { mandatory: false, name: 'priority' },
                ],
            },
            id: 'servers',
        }
        await wrapper.setProps({
            objType: 'servers',
            showAdvanceToggle: true,
            modules: [mockServerModule],
        })
        await wrapper.setData({ selectedModule: mockServerModule })
        expect(wrapper.vm.moduleParameters.map(param => param.name)).to.eqls(
            wrapper.vm.specialParams
        )
    })
})
