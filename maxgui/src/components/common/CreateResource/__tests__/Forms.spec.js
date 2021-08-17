/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { expect } from 'chai'
import mount from '@tests/unit/setup'
import {
    all_modules_map_stub,
    itemSelectMock,
    showDialogMock,
    hideDialogMock,
    routeChangesMock,
} from '@tests/unit/utils'
import Forms from '@CreateResource/Forms'
import sinon from 'sinon'

/**
 * This function tests whether text is transform correctly based on route changes.
 * It should capitalize first letter of current route name if current page is a dashboard page.
 * For dashboard page, it should also transform plural route name to a singular word,
 * i.e., services become Service
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} path Current route path
 * @param {String} selectedForm Selected resource to be created
 */
async function testingTextTransform(wrapper, path, selectedForm) {
    await routeChangesMock(wrapper, path)
    await showDialogMock(wrapper)
    expect(wrapper.vm.$data.selectedForm).to.be.equal(selectedForm)
}

/**
 * This function mockup the selection of resource to be created
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} resourceType resource to be created
 */
async function mockupResourceSelect(wrapper, resourceType) {
    await showDialogMock(wrapper)
    await itemSelectMock(wrapper, resourceType, '.resource-select')
}

/**
 * This function test if form dialog is close accurately
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} buttonClass button class: save, cancel, close
 */
async function testCloseModal(wrapper, buttonClass) {
    await mockupResourceSelect(wrapper, 'Server')
    await wrapper.setData({
        resourceId: 'test-server',
    })
    const btn = wrapper.find(`.${buttonClass}`)
    await btn.trigger('click')
    await wrapper.setProps({ value: false })
    expect(wrapper.vm.computeShowDialog).to.be.false
}

describe('Forms.vue', () => {
    let wrapper, axiosStub, axiosPostStub

    beforeEach(async () => {
        localStorage.clear()

        wrapper = mount({
            shallow: false,
            component: Forms,
            props: {
                value: false, // control visibility of the dialog
                closeModal: () => null,
            },
            computed: {
                all_modules_map: () => all_modules_map_stub,
                form_type: () => null,
            },
        })
        axiosStub = sinon.stub(wrapper.vm.$axios, 'get').resolves(
            Promise.resolve({
                data: {},
            })
        )
        axiosPostStub = sinon.stub(wrapper.vm.$axios, 'post').resolves(Promise.resolve({}))
    })

    afterEach(async function() {
        await axiosStub.restore()
        await axiosPostStub.restore()
        await hideDialogMock(wrapper)
    })

    it(`Should show forms dialog when v-model value changes`, async () => {
        // go to page where '+ Create New' button is visible
        await routeChangesMock(wrapper, '/dashboard/services')
        await showDialogMock(wrapper)
        expect(wrapper.vm.computeShowDialog).to.be.true
    })

    it(`Should auto select form Service if current route name
      doesn't match resource selection items which are 'Service, Server,
      Monitor, Filter, Listener'`, async () => {
        // mockup navigating to sessions tab on dashboard page
        await testingTextTransform(wrapper, '/dashboard/sessions', 'Service')
    })

    it(`Should auto select Service form when current page = /dashboard/services`, async () => {
        await testingTextTransform(wrapper, '/dashboard/services', 'Service')
    })
    it(`Should auto select Server form when current page = /dashboard/servers`, async () => {
        await testingTextTransform(wrapper, '/dashboard/servers', 'Server')
    })
    it(`Should auto select Server form when current page is a server details page`, async () => {
        await testingTextTransform(wrapper, '/dashboard/servers/test-server', 'Server')
    })
    it(`Should auto select Monitor form when current page is a monitor details page`, async () => {
        await testingTextTransform(wrapper, '/dashboard/monitors/test-monitor', 'Monitor')
    })
    it(`Should auto select Service form when current page is a service details page`, async () => {
        await testingTextTransform(wrapper, '/dashboard/services/test-service', 'Service')
    })

    it(`Should assign accurate Router module type object to resourceModules state`, async () => {
        await mockupResourceSelect(wrapper, 'Service')
        expect(wrapper.vm.$data.resourceModules).to.be.deep.equals(all_modules_map_stub['Router'])
    })
    it(`Should assign accurate servers module type object to resourceModules state`, async () => {
        await mockupResourceSelect(wrapper, 'Server')
        expect(wrapper.vm.$data.resourceModules).to.be.deep.equals(all_modules_map_stub['servers'])
    })
    it(`Should assign accurate Monitor module object to resourceModules state`, async () => {
        await mockupResourceSelect(wrapper, 'Monitor')
        expect(wrapper.vm.$data.resourceModules).to.be.deep.equals(all_modules_map_stub['Monitor'])
    })
    it(`Should assign accurate Filter module object to resourceModules state`, async () => {
        await mockupResourceSelect(wrapper, 'Filter')
        expect(wrapper.vm.$data.resourceModules).to.be.deep.equals(all_modules_map_stub['Filter'])
    })

    it(`Should transform authenticator parameter from string type to enum type when
      creating a listener`, async () => {
        await mockupResourceSelect(wrapper, 'Listener')
        let authenticators = all_modules_map_stub['Authenticator']
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
        await testCloseModal(wrapper, 'save')
    })

    it(`Should call closeModal function props to close form dialog
      when "close" button is clicked`, async () => {
        await testCloseModal(wrapper, 'close')
    })

    it(`Should call closeModal function props to close form dialog
      when "cancel" button is clicked`, async () => {
        await testCloseModal(wrapper, 'cancel')
    })
})
