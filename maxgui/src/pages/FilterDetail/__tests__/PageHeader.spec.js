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

import mount from '@tests/unit/setup'
import PageHeader from '@/pages/FilterDetail/PageHeader'

import { dummy_all_filters, triggerBtnClick, openConfirmDialog } from '@tests/unit/utils'

describe('FilterDetail - PageHeader', () => {
    let wrapper, axiosStub

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: PageHeader,
            propsData: {
                currentFilter: dummy_all_filters[0],
            },
        })
        axiosStub = sinon.stub(wrapper.vm.$store.$http, 'delete').returns(Promise.resolve())
    })

    afterEach(() => {
        axiosStub.restore()
    })

    it(`Should pass necessary props & attrs to confirm-dialog`, () => {
        const confirmDialog = wrapper.findComponent({ name: 'confirm-dialog' })
        expect(confirmDialog.exists()).to.be.true
        const { type, item } = confirmDialog.vm.$props
        const { title, onSave, value } = confirmDialog.vm.$attrs
        const { dialogTitle, dialogType, isConfDlgOpened } = wrapper.vm.$data
        expect(value).to.be.equals(isConfDlgOpened)
        expect(title).to.be.equals(dialogTitle)
        expect(type).to.be.equals(dialogType)
        expect(item).to.be.deep.equals(wrapper.vm.$props.currentFilter)
        expect(onSave).to.be.equals(wrapper.vm.confirmSave)
    })

    it(`Should open confirm-dialog when delete button is clicked`, async () => {
        await openConfirmDialog({ wrapper, cssSelector: '.delete-btn' })
        const confirmDialog = wrapper.findComponent({ name: 'confirm-dialog' })
        expect(confirmDialog.vm.$attrs.value).to.be.true
    })

    it(`Should send delete request after confirming delete`, async () => {
        await openConfirmDialog({ wrapper, cssSelector: '.delete-btn' })
        const confirmDialog = wrapper.findComponent({ name: 'confirm-dialog' })
        await triggerBtnClick(confirmDialog, '.save')

        await axiosStub.should.have.been.calledWith(`/filters/${dummy_all_filters[0].id}?force=yes`)
    })
})
