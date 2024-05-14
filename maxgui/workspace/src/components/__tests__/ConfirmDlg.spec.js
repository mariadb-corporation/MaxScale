/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import ConfirmDlg from '@wsComps/ConfirmDlg.vue'

const confirmDlgDataStub = {
    is_opened: false,
    title: 'Test title',
    confirm_msg: 'Test message',
    save_text: 'save',
    cancel_text: 'dontSave',
    on_save: () => null,
    after_cancel: () => null,
}
describe('ConfirmDlg', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mount({
            shallow: true,
            component: ConfirmDlg,
            computed: {
                confirm_dlg: () => confirmDlgDataStub,
            },
        })
    })

    afterEach(() => sinon.restore())

    it('Should pass accurate data to mxs-dlg', () => {
        const {
            value,
            title,
            closeImmediate,
            lazyValidation,
            onSave,
            cancelText,
            saveText,
        } = wrapper.findComponent({
            name: 'mxs-dlg',
        }).vm.$props
        expect(value).to.equals(wrapper.vm.isOpened)
        expect(title).to.equals(wrapper.vm.confirm_dlg.title)
        expect(closeImmediate).to.be.true
        expect(lazyValidation).to.be.false
        expect(onSave).to.be.eql(wrapper.vm.confirm_dlg.on_save)
        expect(cancelText).to.equals(wrapper.vm.confirm_dlg.cancel_text)
        expect(saveText).to.equals(wrapper.vm.confirm_dlg.save_text)
    })

    it(`Should return accurate value for isOpened`, () => {
        expect(wrapper.vm.isOpened).to.equal(wrapper.vm.confirm_dlg.is_opened)
    })

    it(`Should call SET_CONFIRM_DLG when isOpened is changed`, () => {
        const spy = sinon.spy(wrapper.vm, 'SET_CONFIRM_DLG')
        wrapper.vm.isOpened = true
        spy.should.have.been.calledOnceWithExactly({ ...confirmDlgDataStub, is_opened: true })
    })
})
