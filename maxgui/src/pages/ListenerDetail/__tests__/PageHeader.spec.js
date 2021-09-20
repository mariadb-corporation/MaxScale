/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import PageHeader from '@/pages/ListenerDetail/PageHeader'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import { dummy_all_listeners, triggerBtnClick, openConfirmDialog } from '@tests/unit/utils'

chai.should()
chai.use(sinonChai)

describe('ListenerDetail - PageHeader', () => {
    let wrapper, axiosStub

    beforeEach(async () => {
        wrapper = mount({
            shallow: false,
            component: PageHeader,
            props: {
                currentListener: dummy_all_listeners[0],
            },
        })
        axiosStub = sinon.stub(wrapper.vm.$axios, 'delete').returns(Promise.resolve())
    })

    afterEach(async () => {
        await axiosStub.restore()
    })

    it(`Should render listener state accurately`, async () => {
        const span = wrapper.find('.resource-state')
        expect(span.exists()).to.be.true
        expect(span.text()).to.be.equals(dummy_all_listeners[0].attributes.state)
    })

    it(`Should pass necessary props to confirm-dialog`, async () => {
        const confirmDialog = wrapper.findComponent({ name: 'confirm-dialog' })
        expect(confirmDialog.exists()).to.be.true
        const { title, type, item, onSave, onClose, onCancel } = confirmDialog.vm.$props
        const { dialogTitle, dialogType } = wrapper.vm.$data

        expect(title).to.be.equals(dialogTitle)
        expect(type).to.be.equals(dialogType)
        expect(item).to.be.deep.equals(wrapper.vm.$props.currentListener)
        expect(onSave).to.be.equals(wrapper.vm.confirmSave)
        expect(onClose).to.be.equals(wrapper.vm.handleClose)
        expect(onCancel).to.be.equals(wrapper.vm.handleClose)
    })

    it(`Should open confirm-dialog when delete button is clicked`, async () => {
        await openConfirmDialog({ wrapper, cssSelector: '.delete-btn' })
        const confirmDialog = wrapper.findComponent({ name: 'confirm-dialog' })
        expect(confirmDialog.vm.$data.isDialogOpen).to.be.true
    })

    it(`Should send delete request after confirming delete`, async () => {
        await openConfirmDialog({ wrapper, cssSelector: '.delete-btn' })
        const confirmDialog = wrapper.findComponent({ name: 'confirm-dialog' })
        await triggerBtnClick(confirmDialog, '.save')

        await axiosStub.should.have.been.calledWith(`/listeners/${dummy_all_listeners[0].id}`)
    })
})
