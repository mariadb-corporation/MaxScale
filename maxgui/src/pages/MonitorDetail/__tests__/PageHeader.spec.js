/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import PageHeader from '@/pages/MonitorDetail/PageHeader'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import {
    dummy_all_monitors,
    triggerBtnClick,
    openConfirmDialog,
    assertSendingRequest,
} from '@tests/unit/utils'

chai.should()
chai.use(sinonChai)

const computedFactory = (computed = {}) =>
    mount({
        shallow: false,
        component: PageHeader,
        props: {
            currentMonitor: dummy_all_monitors[0],
        },
        computed,
    })

const ALL_BTN_CLASS_PREFIXES = ['stop', 'start', 'delete']

describe('MonitorDetail - PageHeader', () => {
    let wrapper, axiosDeleteStub, axiosPutStub

    beforeEach(async () => {
        wrapper = mount({
            shallow: false,
            component: PageHeader,
            props: {
                currentMonitor: dummy_all_monitors[0],
            },
        })
        axiosDeleteStub = sinon.stub(wrapper.vm.$axios, 'delete').returns(Promise.resolve())
        axiosPutStub = sinon.stub(wrapper.vm.$axios, 'put').returns(Promise.resolve())
    })

    afterEach(async () => {
        await axiosDeleteStub.restore()
        await axiosPutStub.restore()
    })

    it(`Should render monitor state accurately`, async () => {
        const span = wrapper.find('.resource-state')
        expect(span.exists()).to.be.true
        expect(span.text()).to.be.equals(dummy_all_monitors[0].attributes.state)
    })

    it(`Should render monitor module accurately`, async () => {
        const span = wrapper.find('.resource-module')
        expect(span.exists()).to.be.true
        expect(span.text()).to.be.equals(dummy_all_monitors[0].attributes.module)
    })

    it(`Should pass necessary props to confirm-dialog`, async () => {
        const confirmDialog = wrapper.findComponent({
            name: 'confirm-dialog',
        })
        expect(confirmDialog.exists()).to.be.true
        const { title, type, item, onSave, onClose, onCancel } = confirmDialog.vm.$props
        const { dialogTitle, dialogType } = wrapper.vm.$data

        expect(title).to.be.equals(dialogTitle)
        expect(type).to.be.equals(dialogType)
        expect(item).to.be.deep.equals(wrapper.vm.$props.currentMonitor)
        expect(onSave).to.be.equals(wrapper.vm.confirmSave)
        expect(onClose).to.be.equals(wrapper.vm.handleClose)
        expect(onCancel).to.be.equals(wrapper.vm.handleClose)
    })

    describe('confirm-dialog opening test assertions', () => {
        const dummyState = ['Running', 'Stopped', 'Running']
        ALL_BTN_CLASS_PREFIXES.forEach((prefix, i) =>
            it(`Should open confirm-dialog when ${prefix} button is clicked`, async () => {
                // getState stub
                wrapper = computedFactory({
                    getState: () => dummyState[i],
                })
                await openConfirmDialog({
                    wrapper,
                    cssSelector: `.${prefix}-btn`,
                })
                expect(wrapper.vm.showConfirmDialog).to.be.true
            })
        )
    })

    describe('button disable test assertions', async () => {
        const dummyState = ['Stopped', 'Running']
        const btnClassPrefixes = ['stop', 'start']
        btnClassPrefixes.forEach((prefix, i) => {
            let des = `Should disable ${prefix} btn when state is: ${dummyState[i]}`
            it(des, async () => {
                // getState stub
                wrapper = computedFactory({
                    getState: () => dummyState[i],
                })
                await triggerBtnClick(wrapper, '.gear-btn')
                const btn = wrapper.find(`.${prefix}-btn`)
                expect(btn.attributes().disabled).to.be.equals('disabled')
            })
        })
    })

    describe('Monitor state update and monitor deletion test assertions', async () => {
        // dummy states that btn can be clicked
        const dummyState = ['Running', 'Stopped', 'Running']
        ALL_BTN_CLASS_PREFIXES.forEach((prefix, i) => {
            // getState stub
            const wrapper = computedFactory({
                getState: () => dummyState[i],
            })
            const cssSelector = `.${prefix}-btn`
            const id = dummy_all_monitors[0].id
            let httpMethod = 'PUT'

            if (prefix === 'delete') httpMethod = 'DELETE'
            const des = `Should send ${httpMethod} request after confirming ${prefix} a monitor`
            it(des, async () => {
                switch (prefix) {
                    case 'stop':
                    case 'start':
                        await assertSendingRequest({
                            wrapper,
                            cssSelector,
                            axiosStub: axiosPutStub,
                            axiosStubCalledWith: `/monitors/${id}/${prefix}`,
                        })
                        break
                    case 'delete':
                        await assertSendingRequest({
                            wrapper,
                            cssSelector,
                            axiosStub: axiosDeleteStub,
                            axiosStubCalledWith: `/monitors/${id}?force=yes`,
                        })
                        break
                }
            })
        })
    })
})
