/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import ConfirmDlg from '@src/components/ConfirmDlg'

describe('ConfirmDlg.vue', () => {
    let wrapper

    let initialAttrs = {
        value: false, // control visibility of the dialog
    }
    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: ConfirmDlg,
            attrs: initialAttrs,
            propsData: {
                type: 'unlink',
                item: null,
                smallInfo: '',
            },
        })
    })

    it(`Should render accurate confirmation text when item is defined`, async () => {
        await wrapper.setProps({ type: 'destroy', item: { id: 'Monitor', type: 'monitors' } })
        let span = wrapper.find('.confirmations-text').html()
        // check include correct span value
        expect(span).to.be.include('Are you sure you want to destroy <b>Monitor</b>?')
    })
    it(`Should not render confirmation text when item is not defined`, async () => {
        await wrapper.setProps({ type: 'destroy', item: null })
        let span = wrapper.find('.confirmations-text')
        expect(span.exists()).to.be.equal(false)
    })

    it(`Testing component renders accurate slot if body-append slot is used`, () => {
        // component need to be shallowed, ignoring child components
        wrapper = mount({
            shallow: false,
            component: ConfirmDlg,
            attrs: initialAttrs,
            slots: {
                'body-append': '<div class="body-append">test div</div>',
            },
        })
        expect(wrapper.find('.body-append').html()).to.be.equal(
            '<div class="body-append">test div</div>'
        )
    })
})
