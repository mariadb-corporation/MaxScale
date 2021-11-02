/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { expect } from 'chai'
import mount from '@tests/unit/setup'
import ConfirmDialog from '@/components/common/Dialogs/ConfirmDialog'

describe('ConfirmDialog.vue', () => {
    let wrapper

    let initialProps = {
        value: false, // control visibility of the dialog
        type: 'unlink', //delete, unlink, destroy, stop, start (lowercase only)
        title: 'Unlink server', // translate before passing

        onSave: () => wrapper.setProps({ value: false }),
        onClose: () => wrapper.setProps({ value: false }),
        onCancel: () => wrapper.setProps({ value: false }),

        //optional props
        item: { id: 'row_server_1', type: 'servers' },
        smallInfo: '', // translate before passing
    }
    beforeEach(() => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: ConfirmDialog,
            props: initialProps,
        })
    })

    it(`Testing component renders accurate confirmation text`, async () => {
        // component need to be shallowed, ignoring child components
        wrapper = mount({
            shallow: true,
            component: ConfirmDialog,
            props: initialProps,
        })
        // get confirmation text span
        let span = wrapper.find('.confirmations-text').html()
        // check include correct span value
        expect(span).to.be.include('Are you sure you want to unlink row_server_1?')

        await wrapper.setProps({ type: 'destroy', item: { id: 'Monitor', type: 'monitors' } })

        span = wrapper.find('.confirmations-text').html()
        // check include correct span value
        expect(span).to.be.include('Are you sure you want to destroy Monitor?')

        await wrapper.setProps({ type: 'destroy', item: null })
        span = wrapper.find('.confirmations-text')
        // Dont render confirmation text span if target item is null
        expect(span.exists()).to.be.equal(false)
    })

    it(`Testing component renders accurate slot if body-append slot is used`, async () => {
        // component need to be shallowed, ignoring child components
        wrapper = mount({
            shallow: true,
            component: ConfirmDialog,
            props: initialProps,
            slots: {
                'body-append': '<div class="body-append">test div</div>',
            },
        })
        expect(wrapper.find('.body-append').html()).to.be.equal(
            '<div class="body-append">test div</div>'
        )
    })
})
