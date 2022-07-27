/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-07-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import WkeToolbar from '@/pages/QueryPage/WkeToolbar'

const mountFactory = opts =>
    mount({
        shallow: false,
        component: WkeToolbar,
        stubs: {
            'readonly-query-editor': "<div class='stub'></div>",
        },
        ...opts,
    })

describe(`wke-toolbar - mounted hook and child component's interaction tests`, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory()
    })
    it('Should emit get-total-btn-width evt in the next tick after component is mounted', () => {
        wrapper.vm.$nextTick(() => {
            expect(wrapper.emitted()).to.have.property('get-total-btn-width')
        })
    })
    it('Should pass accurate data to query-cnf-dlg-ctr via attrs', () => {
        const cnfDlg = wrapper.findComponent({ name: 'query-cnf-dlg-ctr' })
        expect(cnfDlg.vm.$attrs.value).to.be.equals(wrapper.vm.queryConfigDialog)
    })
    it(`Should emit on-fullscreen-click event`, () => {
        let wrapper = mountFactory()
        const btn = wrapper.find('.min-max-btn')
        btn.trigger('click')
        expect(wrapper.emitted()).to.have.property('on-fullscreen-click')
    })

    it(`Should popup query setting dialog`, () => {
        let wrapper = mountFactory()
        expect(wrapper.vm.queryConfigDialog).to.be.false
        wrapper.find('.query-setting-btn').trigger('click')
        expect(wrapper.vm.queryConfigDialog).to.be.true
    })

    it(`Should emit on-add-wke event`, async () => {
        let isEmitted = false
        let wrapper = mountFactory({
            propsData: { isAddWkeDisabled: false },
            listeners: {
                'on-add-wke': () => (isEmitted = true),
            },
        })
        await wrapper.find('.add-wke-btn').trigger('click')
        expect(isEmitted).to.be.true
    })
})
