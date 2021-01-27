/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { expect } from 'chai'
import mount from '@tests/unit/setup'
import BaseDialog from '@/components/common/Dialogs/BaseDialog'

/**
 * This function mockups the action of opening a dialog
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 */
export async function showDialogMock(wrapper) {
    await wrapper.setProps({
        value: true,
    })
}

describe('BaseDialog.vue', () => {
    let wrapper
    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: BaseDialog,
            props: {
                value: true,
                title: 'dialog title',
                onSave: () => wrapper.setProps({ value: false }),
            },
        })
    })

    it('Should close when cancel button is pressed', async () => {
        // make dialog open
        await showDialogMock(wrapper)
        let count = 0
        wrapper.vm.$on('input', isOpen => {
            expect(isOpen).to.be.false
            ++count
        })
        await wrapper.find('.cancel').trigger('click')
        expect(count).to.be.equals(1)
    })

    it('Should close when close button is pressed', async () => {
        // make dialog open
        await showDialogMock(wrapper)
        let count = 0
        wrapper.vm.$on('input', isOpen => {
            expect(isOpen).to.be.false
            ++count
        })
        await wrapper.find('.close').trigger('click')
        expect(count).to.be.equals(1)
    })

    it('dialog closes when save button is pressed', async () => {
        // make dialog open
        await showDialogMock(wrapper)
        let count = 0
        wrapper.vm.$on('input', isOpen => {
            expect(isOpen).to.be.false
            ++count
        })
        await wrapper.find('.save').trigger('click')
        // onSave is an async cb fb, so input event will be called in the nextTick
        await wrapper.vm.$nextTick()
        expect(count).to.be.equals(1)
    })
})
