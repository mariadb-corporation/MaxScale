/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { expect } from 'chai'
import mount from '@tests/unit/setup'
import { itemSelectMock, inputChangeMock } from '@tests/unit/utils'
import ModuleParameters from '@CreateResource/ModuleParameters'

const mockupModules = [
    {
        attributes: {
            module_type: 'Router',
            parameters: [
                {
                    default_value: false,
                    mandatory: false,
                    name: 'delayed_retry',
                    type: 'bool',
                },
            ],
        },
        id: 'readwritesplit',
    },
    {
        attributes: {
            module_type: 'Router',
            parameters: [
                {
                    default_value: true,
                    mandatory: false,
                    name: 'log_auth_warnings',
                    type: 'bool',
                },
                {
                    default_value: '',
                    mandatory: false,
                    name: 'string-param',
                    type: 'string',
                },
            ],
        },
        id: 'readconnroute',
    },
]
const moduleName = 'router'

describe('ModuleParameters.vue', () => {
    let wrapper

    beforeEach(async () => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: ModuleParameters,
            props: {
                modules: mockupModules,
                // for displaying label
                moduleName: moduleName, // 'server', 'module', 'protocol'
            },
        })
    })

    it(`Should render module name as input label accurately`, async () => {
        const arrayClasses = wrapper.find('.label').classes()
        const strClasses = arrayClasses.toString().replace(/,/g, ' ')
        expect(strClasses).to.be.equals('text-capitalize label color text-small-text d-block')

        expect(wrapper.find('.label').text()).to.be.equals('router')
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

        const parametersCollapse = wrapper.findComponent({ name: 'parameters-collapse' })
        const {
            parameters,
            usePortOrSocket,
            isTree,
            parentForm,
            isListener,
        } = parametersCollapse.vm.$props
        // props
        expect(parameters).to.be.deep.equals(wrapper.vm.moduleParameters)
        expect(usePortOrSocket).to.be.deep.equals(wrapper.vm.$props.usePortOrSocket)
        expect(isTree).to.be.deep.equals(wrapper.vm.$props.isTree)
        expect(parentForm).to.be.deep.equals(wrapper.vm.$props.parentForm)
        expect(isListener).to.be.deep.equals(wrapper.vm.$props.isListener)
        //ref
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
})
