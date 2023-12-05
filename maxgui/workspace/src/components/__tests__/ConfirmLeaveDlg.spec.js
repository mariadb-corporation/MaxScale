/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import ConfirmLeaveDlg from '@wsComps/ConfirmLeaveDlg.vue'

describe('ConfirmLeaveDlg', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mount({
            shallow: true,
            component: ConfirmLeaveDlg,
            propsData: { value: true },
        })
    })

    it('confirmDelAll should be true by default', () => {
        expect(wrapper.vm.$data.confirmDelAll).to.be.true
    })

    it('Should pass accurate data to mxs-dlg', () => {
        const { value, title, saveText, onSave } = wrapper.findComponent({
            name: 'mxs-dlg',
        }).vm.$props
        expect(title).to.be.equals(wrapper.vm.$mxs_t('confirmations.leavePage'))
        expect(value).to.be.equals(wrapper.vm.$attrs.value)
        expect(saveText).to.be.equals('confirm')
        expect(onSave).to.be.eql(wrapper.vm.onConfirm)
    })

    it('Should show disconnect all info', () => {
        expect(wrapper.find('[data-test="disconnect-info"]').html()).to.contain(
            wrapper.vm.$mxs_t('info.disconnectAll')
        )
    })

    it('Should pass accurate data to v-checkbox', () => {
        const { value, label, hideDetails } = wrapper.findComponent({
            name: 'v-checkbox',
        }).vm.$props
        expect(value).to.be.equals(wrapper.vm.$data.value)
        expect(label).to.be.equals(wrapper.vm.$mxs_t('disconnectAll'))
        expect(hideDetails).to.be.true
    })

    it('Should emit on-confirm event with accurate argument', async () => {
        await wrapper.setData({ confirmDelAll: false })
        wrapper.vm.onConfirm()
        expect(wrapper.emitted('on-confirm')[0]).to.be.eql([false])
    })
})
