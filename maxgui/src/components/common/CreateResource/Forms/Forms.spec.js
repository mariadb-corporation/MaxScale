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
import Forms from '@CreateResource/Forms'
import moxios from 'moxios'
import Vuex from 'vuex'
//a minimized mockup allModulesMap state from vuex store
const allModulesMap = {
    servers: [
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
                ],
            },
            id: 'servers',
        },
    ],
    Filter: [
        {
            attributes: {
                module_type: 'Filter',
                parameters: [
                    {
                        mandatory: true,
                        name: 'inject',
                        type: 'quoted string',
                    },
                ],
            },
            id: 'comment',
        },
    ],
    Authenticator: [
        {
            attributes: {
                module_type: 'Authenticator',
                parameters: [],
            },
            id: 'MariaDBAuth',
        },
    ],
    Monitor: [
        {
            attributes: {
                module_type: 'Monitor',
                parameters: [
                    {
                        default_value: '/cmapi/0.4.0',
                        mandatory: false,
                        name: 'admin_base_path',
                        type: 'string',
                    },
                ],
            },
            id: 'csmon',
        },
    ],
    Router: [
        {
            attributes: {
                module_type: 'Router',
                parameters: [
                    {
                        default_value: 'false',
                        mandatory: false,
                        name: 'delayed_retry',
                        type: 'bool',
                    },
                ],
            },
            id: 'readwritesplit',
        },
    ],
    Protocol: [
        {
            attributes: {
                module_type: 'Protocol',
                parameters: [
                    {
                        mandatory: true,
                        name: 'protocol',
                        type: 'string',
                    },
                ],
                version: 'V1.1.0',
            },
            id: 'mariadbclient',
        },
    ],
}

/**
 * This function mockup the action to open form dialog
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 */
async function mockupOpeningDialog(wrapper) {
    await wrapper.setProps({
        value: true,
    })
}

/**
 * This function mockup the action to close form dialog
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 */
async function mockupClosingDialog(wrapper) {
    await wrapper.setProps({
        value: false,
    })
}

/**
 * This function tests whether text is transform correctly based on route changes.
 * It should capitalize first letter of current route name if current page is a dashboard page.
 * For dashboard page, it should also transform plural route name to a singular word,
 * i.e., services become Service
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} route Current route path
 * @param {String} selectedResource Selected resource to be created
 */
async function testingTextTransform(wrapper, route, selectedResource) {
    await wrapper.vm.$router.push(route)
    await mockupOpeningDialog(wrapper)
    expect(wrapper.vm.$data.selectedResource).to.be.equal(selectedResource)
    await mockupClosingDialog(wrapper)
}

/**
 * This function mockup the selection of resource to be created
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} resourceType resource to be created
 */
async function mockupResourceSelect(wrapper, resourceType) {
    wrapper.vm.$route.name !== 'sessions' && (await wrapper.vm.$router.push('/dashboard/sessions'))
    !wrapper.vm.computeShowDialog && (await mockupOpeningDialog(wrapper))

    let vSelect = wrapper.find('.resource-select')
    await vSelect.vm.selectItem(resourceType)
}

/**
 * This function test if form dialog is close accurately
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} buttonClass button class: save, cancel, close
 */
async function testCloseModal(wrapper, buttonClass) {
    wrapper.vm.$route.name !== 'servers' && (await mockupResourceSelect(wrapper, 'Server'))
    let count = 0
    await wrapper.setProps({
        closeModal: async () => {
            count++
            await wrapper.setProps({ value: false })
        },
    })
    await wrapper.setData({
        resourceId: 'test-server',
    })
    await wrapper.vm.$nextTick()
    await wrapper.find(`.${buttonClass}`).trigger('click')
    expect(wrapper.vm.computeShowDialog).to.be.false
    expect(count).to.be.equals(1)
}
describe('Forms.vue', () => {
    let wrapper
    let getters, actions, store

    beforeEach(() => {
        localStorage.clear()
        getters = {
            'maxscale/allModulesMap': () => allModulesMap,
            'service/allServices': () => [],
            'service/allServicesInfo': () => ({}),
            'server/allServers': () => [],
            'server/allServersInfo': () => ({}),
            'monitor/allMonitorsInfo': () => ({}),
            'monitor/allMonitors': () => [],
            'filter/allFiltersInfo': () => ({}),
            'filter/allFilters': () => [],
            'listener/allListenersInfo': () => ({}),
        }
        actions = {
            'service/createService': () => null,
            'monitor/createMonitor': () => null,
            'filter/createFilter': () => null,
            'listener/createListener': () => null,
            'server/createServer': () => null,
            'service/fetchAllServices': () => null,
            'server/fetchAllServers': () => null,
            'monitor/fetchAllMonitors': () => null,
            'filter/fetchAllFilters': () => null,
            'listener/fetchAllListeners': () => null,
        }
        store = new Vuex.Store({
            getters,
            actions,
            mutations: {
                showOverlay: () => null,
                hideOverlay: () => null,
            },
        })
        wrapper = mount({
            shallow: false,
            component: Forms,
            props: {
                value: false, // control visibility of the dialog
            },
            store,
        })
        moxios.install(wrapper.vm.axios)
    })

    afterEach(async function() {
        moxios.uninstall(wrapper.vm.axios)
        await mockupClosingDialog(wrapper)
        //push back to settings page
        wrapper.vm.$route.name !== 'settings' && (await wrapper.vm.$router.push('/settings'))
    })

    it(`Should show forms dialog when v-model value changes`, async () => {
        // go to page where '+ Create New' button is visible
        wrapper.vm.$route.name !== 'servers' &&
            (await wrapper.vm.$router.push('/dashboard/servers'))
        await mockupOpeningDialog(wrapper)
        expect(wrapper.vm.computeShowDialog).to.be.true
    })

    it(`Should auto select form Service if current route name
      doesn't match resource selection items which are 'Service, Server,
      Monitor, Filter, Listener'`, async () => {
        // mockup navigating to sessions tab on dashboard page
        await testingTextTransform(wrapper, '/dashboard/sessions', 'Service')
    })

    it(`Should auto select form resource based on route changes`, async () => {
        // mockup navigating to services tab on dashboard page
        await testingTextTransform(wrapper, '/dashboard/services', 'Service')
        // mockup navigating to servers tab on dashboard page
        await testingTextTransform(wrapper, '/dashboard/servers', 'Server')
        // mockup navigating to a server details page
        await testingTextTransform(wrapper, '/dashboard/servers/test-server', 'Server')
        // mockup navigating to a monitor details page
        await testingTextTransform(wrapper, '/dashboard/monitors/test-monitor', 'Monitor')
        // mockup navigating to a service details page
        await testingTextTransform(wrapper, '/dashboard/services/test-service', 'Service')
    })

    it(`Should assign accurate module type object to resourceModules state`, async () => {
        await mockupResourceSelect(wrapper, 'Service')
        expect(wrapper.vm.$data.resourceModules).to.be.deep.equals(allModulesMap['Router'])
        await mockupResourceSelect(wrapper, 'Server')
        expect(wrapper.vm.$data.resourceModules).to.be.deep.equals(allModulesMap['servers'])
        await mockupResourceSelect(wrapper, 'Monitor')
        expect(wrapper.vm.$data.resourceModules).to.be.deep.equals(allModulesMap['Monitor'])
        await mockupResourceSelect(wrapper, 'Filter')
        expect(wrapper.vm.$data.resourceModules).to.be.deep.equals(allModulesMap['Filter'])
    })

    it(`Should transform authenticator parameter from string type to enum type when
      creating a listener`, async () => {
        await mockupResourceSelect(wrapper, 'Listener')
        let authenticators = allModulesMap['Authenticator']
        let authenticatorId = authenticators.map(item => `${item.id}`)
        wrapper.vm.$data.resourceModules.forEach(protocol => {
            let authenticatorParamObj = protocol.attributes.parameters.find(
                o => o.name === 'authenticator'
            )
            if (authenticatorParamObj) {
                expect(authenticatorParamObj.type).to.be.equals('enum')
                expect(authenticatorParamObj.enum_values).to.be.deep.equals(authenticatorId)
                expect(authenticatorParamObj.type).to.be.equals('')
            }
        })
    })

    it(`Should add hyphen when resourceId contains whitespace`, async () => {
        await mockupResourceSelect(wrapper, 'Monitor')
        await wrapper.setData({
            resourceId: 'test monitor',
        })
        expect(wrapper.vm.$data.resourceId).to.be.equals('test-monitor')
    })

    it(`Should validate resourceId when there is duplicated resource name`, async () => {
        await mockupResourceSelect(wrapper, 'Monitor')
        // mockup validateInfo
        await wrapper.setData({
            validateInfo: { ...wrapper.vm.$data.validateInfo, idArr: ['test-monitor'] },
        })
        await wrapper.setData({
            resourceId: 'test-monitor',
        })
        await wrapper.vm.$nextTick(() => {
            let vTextField = wrapper.find('.resource-id')
            let errorMessageDiv = vTextField.find('.v-messages__message').html()
            expect(errorMessageDiv).to.be.include('test-monitor already exists')
        })
    })

    it(`Should validate resourceId when it is empty`, async () => {
        await mockupResourceSelect(wrapper, 'Monitor')

        await wrapper.setData({
            resourceId: 'test-monitor',
        })
        await wrapper.vm.$nextTick()
        await wrapper.setData({
            resourceId: '',
        })

        await wrapper.vm.$nextTick(() => {
            let vTextField = wrapper.find('.resource-id')
            let errorMessageDiv = vTextField.find('.v-messages__message').html()
            expect(errorMessageDiv).to.be.include('id is required')
        })
    })
    it(`Should call closeModal function props to close form dialog
      when "save" button is clicked`, async () => {
        testCloseModal(wrapper, 'save')
    })

    it(`Should call closeModal function props to close form dialog
    when "close" button is clicked`, async () => {
        testCloseModal(wrapper, 'close')
    })

    it(`Should call closeModal function props to close form dialog
    when "cancel" button is clicked`, async () => {
        testCloseModal(wrapper, 'cancel')
    })
})
