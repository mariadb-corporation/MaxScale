/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import ReconnDlgCtr from '@wsComps/ReconnDlgCtr.vue'

const lost_cnn_stub = {
    id: '123',
    meta: { name: 'server_0' },
    lost_cnn_err: { message: 'Lost connection to server during query', errno: 2013 },
}
const mountFactory = opts =>
    mount({
        shallow: true,
        component: ReconnDlgCtr,
        ...opts,
    })
describe('ReconnDlgCtr', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mountFactory()
    })

    afterEach(() => sinon.restore())

    it('Should show reconnection dialog when there is a connection error', () => {
        expect(wrapper.vm.showReconnDialog).to.be.false
        wrapper = mountFactory({
            computed: {
                activeConns: () => [lost_cnn_stub],
            },
        })
        expect(wrapper.vm.showReconnDialog).to.be.true
    })
    it('Should pass accurate data to mxs-dlg via props', () => {
        const baseDialog = wrapper.findComponent({ name: 'mxs-dlg' })
        const { value, title, cancelText, saveText, onSave, showCloseBtn } = baseDialog.vm.$props
        expect(title).to.be.equals('Server has gone away')
        expect(value).to.be.equals(wrapper.vm.showReconnDialog)
        expect(cancelText).to.be.equals('disconnect')
        expect(saveText).to.be.equals('reconnect')
        expect(onSave).to.be.equals(wrapper.vm.handleReconnect)
        expect(showCloseBtn).to.be.false
    })
    it('Should call deleteConn when clicking disconnect button ', async () => {
        const spy = sinon.spy(ReconnDlgCtr.methods, 'deleteConns')
        wrapper = mountFactory({ shallow: false })
        await wrapper.find('.cancel').trigger('click') // cancel btn is disconnect btn
        spy.should.have.been.calledOnce
    })
})
