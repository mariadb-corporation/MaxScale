import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import { all_modules_map_stub, itemSelectMock, routeChangesMock } from '@tests/unit/utils'
import CreateResource from '@CreateResource'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
chai.should()
chai.use(sinonChai)

/**
 * This function mockup the selection of resource to be created
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} resourceType resource to be created
 */
async function mockupResourceSelect(wrapper, resourceType) {
    await wrapper.setData({ isDialogOpen: true })
    await itemSelectMock(wrapper, resourceType, '.resource-select')
}

const defaultComputed = {
    all_modules_map: () => all_modules_map_stub,
    form_type: () => null,
}
const computedFactory = (computed = defaultComputed) =>
    mount({
        shallow: false,
        component: CreateResource,
        computed,
    })

describe('CreateResource index', () => {
    let wrapper, axiosStub

    beforeEach(async () => {
        wrapper = computedFactory()
        axiosStub = sinon.stub(wrapper.vm.$axios, 'get').resolves(
            Promise.resolve({
                data: {},
            })
        )
    })

    afterEach(async function() {
        await wrapper.setData({ isDialogOpen: false })
        await axiosStub.restore()
    })

    it(`Should not open base-dialog form when component is rendered `, () => {
        const baseDialog = wrapper.findComponent({
            name: 'base-dialog',
        })
        expect(baseDialog.vm.$props.value).to.be.false
    })

    it(`Should fetch all modules and open creation dialog form
      when '+ Create New' button is clicked `, async () => {
        await wrapper.find('button').trigger('click')
        await axiosStub.should.have.been.calledWith('/maxscale/modules?load=all')
        await wrapper.vm.$nextTick(() => {
            expect(wrapper.vm.$data.isDialogOpen).to.be.true
        })
    })

    it(`Should show base-dialog when isDialogOpen changes`, async () => {
        // go to page where '+ Create New' button is visible
        await routeChangesMock(wrapper, '/dashboard/services')
        await wrapper.setData({ isDialogOpen: true })
        const baseDialog = wrapper.findComponent({ name: 'base-dialog' })
        expect(baseDialog.vm.$props.value).to.be.true
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

    describe('Should set form type by route name', () => {
        const routes = [
            {
                path: '/dashboard/sessions',
                formName: 'Service',
            },
            {
                path: '/dashboard/servers/test-server',
                formName: 'Server',
            },
            {
                path: '/dashboard/services',
                formName: 'Service',
            },
            {
                path: '/dashboard/servers',
                formName: 'Server',
            },

            {
                path: '/dashboard/monitors/test-monitor',
                formName: 'Monitor',
            },
            {
                path: '/dashboard/services/test-service',
                formName: 'Service',
            },
        ]
        afterEach(async function() {
            await wrapper.destroy()
        })
        routes.forEach(({ path, formName }) => {
            let des = `Should select ${formName} form when current path is ${path}`
            it(des, async () => {
                wrapper = computedFactory()
                await routeChangesMock(wrapper, path)
                await wrapper.setData({ isDialogOpen: true })
                expect(wrapper.vm.$data.selectedForm).to.be.equal(formName)
            })
        })
    })

    describe('Should assign accurate same module_type to resourceModules state', () => {
        const arr = [
            { moduleType: 'Router', formType: 'Service' },
            { moduleType: 'servers', formType: 'Server' },
            { moduleType: 'Monitor', formType: 'Monitor' },
            { moduleType: 'Filter', formType: 'Filter' },
            { moduleType: 'Protocol', formType: 'Listener' },
        ]
        afterEach(async function() {
            await wrapper.destroy()
        })
        arr.forEach(({ moduleType, formType }) => {
            let des = `Should assign accurate ${moduleType} module object to resourceModules state`
            it(des, async () => {
                wrapper = computedFactory()
                await mockupResourceSelect(wrapper, formType)
                expect(wrapper.vm.$data.resourceModules).to.be.deep.equals(
                    all_modules_map_stub[moduleType]
                )
            })
        })
    })
    describe('Should close base-dialog', () => {
        const btns = ['save', 'close', 'cancel']
        afterEach(async function() {
            await wrapper.destroy()
        })
        btns.forEach(btnClass => {
            let des = `Close it when "${btnClass}" button is clicked`
            it(des, async () => {
                wrapper = computedFactory()
                await mockupResourceSelect(wrapper, 'Server')
                await wrapper.setData({
                    resourceId: 'test-server',
                })
                const btn = wrapper.find(`.${btnClass}`)
                await btn.trigger('click')
                await wrapper.setData({ isDialogOpen: false })
                const baseDialog = wrapper.findComponent({ name: 'base-dialog' })
                expect(baseDialog.vm.$props.value).to.be.false
            })
        })
    })
})
