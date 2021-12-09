/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import ReconnDialog from '@/pages/QueryPage/ReconnDialog'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'

chai.should()
chai.use(sinonChai)

const dummy_conn_err_obj = {
    message: 'Lost connection to server during query',
    errno: 2013,
}
const mountFactory = opts =>
    mount({
        shallow: true,
        component: ReconnDialog,
        ...opts,
    })
describe('ReconnDialog', () => {
    let wrapper, deleteConnSpy

    beforeEach(() => {
        deleteConnSpy = sinon.spy(ReconnDialog.methods, 'deleteConn')
        wrapper = mountFactory()
    })
    afterEach(() => {
        deleteConnSpy.restore()
    })
    it('Should show reconnection dialog when there is a connection error', () => {
        expect(wrapper.vm.showReconnDialog).to.be.false
        wrapper = mountFactory({
            computed: {
                getQueryErrMsgObj: () => dummy_conn_err_obj,
            },
        })
        expect(wrapper.vm.showReconnDialog).to.be.true
    })
    it('Should pass accurate data to base-dialog via props', () => {
        const baseDialog = wrapper.findComponent({ name: 'base-dialog' })
        const { value, title, cancelText, saveText, onSave, showCloseIcon } = baseDialog.vm.$props
        expect(title).to.be.equals(wrapper.vm.queryErrMsg)
        expect(value).to.be.equals(wrapper.vm.showReconnDialog)
        expect(cancelText).to.be.equals('disconnect')
        expect(saveText).to.be.equals('reconnect')
        expect(onSave).to.be.equals(wrapper.vm.reconnect)
        expect(showCloseIcon).to.be.false
    })
    it('Should call deleteConn when clicking disconnect button ', async () => {
        wrapper = mountFactory({ shallow: false })
        await wrapper.find('.cancel').trigger('click') // cancel btn is disconnect btn
        deleteConnSpy.should.have.been.calledOnce
    })
})
