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

import mount from '@tests/unit/setup'
import LoginForm from '@/pages/Login/LoginForm'
import { inputChangeMock, routeChangesMock } from '@tests/unit/utils'

import { makeServer } from '@tests/unit/mirage/api'

/**
 * This function mockup checking remember me box
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 */
async function mockupCheckingTheBox(wrapper) {
    const vCheckBox = wrapper.findComponent({ name: 'v-checkbox' })
    const input = vCheckBox.find('input')
    await input.trigger('click')
}

let api
let originalXMLHttpRequest = XMLHttpRequest
describe('LoginForm.vue', () => {
    let wrapper
    beforeEach(() => {
        api = makeServer({ environment: 'test' })
        // eslint-disable-next-line no-global-assign
        XMLHttpRequest = window.XMLHttpRequest
        wrapper = mount({
            shallow: false,
            component: LoginForm,
        })
        routeChangesMock(wrapper, '/login')
    })
    afterEach(() => {
        api.shutdown()
        // Restore node's original window.XMLHttpRequest.
        // eslint-disable-next-line no-global-assign
        XMLHttpRequest = originalXMLHttpRequest
    })
    it('Should render username and password fields.', () => {
        const emailInput = wrapper.find('#username')
        const passwordInput = wrapper.find('#password')

        expect(emailInput.exists()).to.be.true
        expect(passwordInput.exists()).to.be.true
    })

    it('Should render login button.', () => {
        const addButton = wrapper.find('.login-btn')
        expect(addButton.exists()).to.be.true
    })

    it('Should disable login button if username or password is empty', () => {
        const addButton = wrapper.find('.login-btn')
        expect(addButton.attributes().disabled).to.be.equals('disabled')
    })

    it('Should not disable login button if form is valid', async () => {
        await inputChangeMock(wrapper, 'admin', '#username')
        await inputChangeMock(wrapper, 'mariadb', '#password')

        const addButton = wrapper.find('.login-btn')
        expect(addButton.attributes().disabled).to.be.not.equals('disabled')
    })

    it('Should call handleSubmit if "Sign in" button is clicked', async () => {
        const spy = sinon.spy(wrapper.vm, 'handleSubmit')
        await inputChangeMock(wrapper, 'admin', '#username')
        await inputChangeMock(wrapper, 'mariadb', '#password')

        await wrapper.find('.login-btn').trigger('click')
        spy.should.have.been.calledOnce
    })

    it(`Should allows to toggle masked password`, async () => {
        await inputChangeMock(wrapper, 'mariadb', '#password')

        expect(wrapper.vm.$data.isPwdVisible).to.be.equal(false)
        const passwordInput = wrapper.find('#password')
        let inputs = passwordInput.findAll('input')
        let toggleMaskPwdBtn = wrapper.findAll('.v-input__append-inner > button')
        expect(inputs.length).to.be.equal(1)
        expect(toggleMaskPwdBtn.length).to.be.equal(1)

        let input = inputs.at(0)
        expect(input.find('[type = "password"]').exists()).to.be.equal(true)

        await toggleMaskPwdBtn.at(0).trigger('click')
        expect(wrapper.vm.$data.isPwdVisible).to.be.equal(true)
        expect(input.find('[type = "text"]').exists()).to.be.equal(true)
    })

    it('Should have rememberMe checked by default', () => {
        expect(wrapper.vm.$data.rememberMe).to.be.true
    })

    it('Should toggle the value of rememberMe', async () => {
        await mockupCheckingTheBox(wrapper) // unchecked
        expect(wrapper.vm.$data.rememberMe).to.be.false
        await mockupCheckingTheBox(wrapper) // checked
        expect(wrapper.vm.$data.rememberMe).to.be.true
    })
    it('Should send auth request when remember me is not chosen', async () => {
        await inputChangeMock(wrapper, 'admin', '#username')
        await inputChangeMock(wrapper, 'mariadb', '#password')
        await mockupCheckingTheBox(wrapper) // unchecked
        await wrapper.find('.login-btn').trigger('click') //submit
        await wrapper.vm.$nextTick(() =>
            expect(api.pretender.unhandledRequests[0].responseURL).to.be.equal('/auth?persist=yes')
        )
    })

    it('Should send auth request when remember me is chosen', async () => {
        await inputChangeMock(wrapper, 'maxskysql', '#username')
        await inputChangeMock(wrapper, 'skysql', '#password')
        await wrapper.find('.login-btn').trigger('click') //submit
        await wrapper.vm.$nextTick(() =>
            expect(api.pretender.unhandledRequests[0].responseURL).to.be.equal(
                '/auth?persist=yes&max-age=86400'
            )
        )
    })

    it('Should navigate to dashboard page once authenticating process is succeed', async () => {
        await inputChangeMock(wrapper, 'maxskysql', '#username')
        await inputChangeMock(wrapper, 'skysql', '#password')
        await wrapper.find('.login-btn').trigger('click') //submit
        await wrapper.vm.$nextTick(() => {
            expect(api.pretender.unhandledRequests[0].responseURL).to.be.equal(
                '/auth?persist=yes&max-age=86400'
            )
            expect(wrapper.vm.$route.path).to.be.equals('/dashboard/servers')
        })
    })
})
