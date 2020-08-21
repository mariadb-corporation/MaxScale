/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
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
import { dummy_all_monitors, triggerBtnClick, openConfirmDialog } from '@tests/unit/utils'

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

    it(`Should open confirm-dialog when delete button is clicked`, async () => {
        await openConfirmDialog({
            wrapper,
            cssSelector: '.delete-btn',
        })
        expect(wrapper.vm.showConfirmDialog).to.be.true
    })

    it(`Should disable stop button if current monitor state === Stopped`, async () => {
        wrapper = computedFactory({
            getState: () => 'Stopped',
        })
        await triggerBtnClick(wrapper, '.gear-btn')
        const stopBtn = wrapper.find('.stop-btn')
        expect(stopBtn.attributes().disabled).to.be.equals('disabled')
    })

    it(`Should disable start button if current monitor state === Running`, async () => {
        wrapper = computedFactory({
            getState: () => 'Running',
        })
        await triggerBtnClick(wrapper, '.gear-btn')
        const startBtn = wrapper.find('.start-btn')
        expect(startBtn.attributes().disabled).to.be.equals('disabled')
    })

    it(`Should open confirm-dialog when stop button is clicked`, async () => {
        await openConfirmDialog({
            wrapper,
            cssSelector: '.stop-btn',
        })
        expect(wrapper.vm.showConfirmDialog).to.be.true
    })

    it(`Should open confirm-dialog when start button is clicked`, async () => {
        wrapper = computedFactory({
            getState: () => 'Stopped',
        })
        await openConfirmDialog({
            wrapper,
            cssSelector: '.start-btn',
        })

        expect(wrapper.vm.showConfirmDialog).to.be.true
    })

    it(`Should send DELETE request after confirming delete`, async () => {
        await openConfirmDialog({
            wrapper,
            cssSelector: '.delete-btn',
        })
        const confirmDialog = wrapper.findComponent({
            name: 'confirm-dialog',
        })
        await triggerBtnClick(confirmDialog, '.save')

        await axiosDeleteStub.should.have.been.calledWith(
            `/monitors/${dummy_all_monitors[0].id}?force=yes`
        )
    })

    it(`Should send PUT request after confirming stopping a monitor`, async () => {
        await openConfirmDialog({
            wrapper,
            cssSelector: '.stop-btn',
        })
        const confirmDialog = wrapper.findComponent({
            name: 'confirm-dialog',
        })
        await triggerBtnClick(confirmDialog, '.save')
        await axiosPutStub.should.have.been.calledWith(`/monitors/${dummy_all_monitors[0].id}/stop`)
    })

    it(`Should send PUT request after confirming starting a monitor`, async () => {
        wrapper = computedFactory({
            getState: () => 'Stopped',
        })

        await openConfirmDialog({
            wrapper,
            cssSelector: '.start-btn',
        })
        const confirmDialog = wrapper.findComponent({ name: 'confirm-dialog' })
        await triggerBtnClick(confirmDialog, '.save')
        await axiosPutStub.should.have.been.calledWith(
            `/monitors/${dummy_all_monitors[0].id}/start`
        )
    })
})
