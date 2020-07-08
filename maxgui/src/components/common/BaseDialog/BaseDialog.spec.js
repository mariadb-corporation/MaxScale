/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { expect } from 'chai'
import mount from '@tests/unit/setup'
import BaseDialog from '@/components/common/BaseDialog'

describe('BaseDialog.vue', () => {
    let wrapper
    beforeEach(() => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: BaseDialog,
            props: {
                value: true,
                title: 'dialog title',
                onCancel: () => wrapper.setProps({ value: false }),
                onClose: () => wrapper.setProps({ value: false }),
                onSave: () => wrapper.setProps({ value: false }),
            },
        })
    })

    it('dialog closes when cancel button or close button is pressed', async () => {
        //----------------case: cancel btn
        expect(wrapper.vm.computeShowDialog).to.be.true
        await wrapper.find('.cancel').trigger('click')
        expect(wrapper.vm.computeShowDialog).to.be.false

        //---------------case: close btn
        // make dialog open again
        await wrapper.setProps({ value: true })
        expect(wrapper.vm.computeShowDialog).to.be.true

        await wrapper.find('.close').trigger('click')
        expect(wrapper.vm.computeShowDialog).to.be.false
    })

    it('dialog closes when save button is pressed', async () => {
        // make dialog open again
        await wrapper.setProps({ value: true })
        expect(wrapper.vm.computeShowDialog).to.be.true

        await wrapper.find('.save').trigger('click')
        expect(wrapper.vm.computeShowDialog).to.be.false
    })
})
