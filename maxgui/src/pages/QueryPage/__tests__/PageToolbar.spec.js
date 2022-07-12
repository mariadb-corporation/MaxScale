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
import PageToolbar from '@/pages/QueryPage/PageToolbar'

const mountFactory = opts =>
    mount({
        shallow: false,
        component: PageToolbar,
        stubs: {
            'readonly-query-editor': "<div class='stub'></div>",
        },
        ...opts,
    })

// stub active_sql_conn
const dummy_active_sql_conn = { id: '1', name: 'server_0', type: 'servers' }
async function clickAddBtnMock(wrapper) {
    await wrapper.find('.add-wke-btn').trigger('click') // click + button
}

describe(`PageToolbar - mounted hook and child component's interaction tests`, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory()
    })
    it('Should emit get-total-btn-width evt in the next tick after component is mounted', () => {
        wrapper.vm.$nextTick(() => {
            expect(wrapper.emitted()).to.have.property('get-total-btn-width')
        })
    })
    it('Should pass accurate data to query-config-dialog via props', () => {
        const cnfDlg = wrapper.findComponent({ name: 'query-config-dialog' })
        expect(cnfDlg.vm.$props.value).to.be.equals(wrapper.vm.queryConfigDialog)
    })
    it('Should pass accurate data to connection-manager', () => {
        wrapper = mountFactory()
        const cnnMan = wrapper.findComponent({ name: 'connection-manager' })
        expect(cnnMan.vm.$props.disabled).to.be.equals(wrapper.vm.getIsConnBusy)
    })
})

describe('PageToolbar - Add new worksheet tests', () => {
    it(`Should only allow to add new worksheet when a worksheet
      is bound to a connection`, async () => {
        let handleAddNewWkeCallCount = 0
        let wrapper = mountFactory({
            computed: {
                sql_conns: () => ({ [dummy_active_sql_conn.id]: dummy_active_sql_conn }),
            },
            methods: {
                // stubs vuex actions
                addNewWs: () => handleAddNewWkeCallCount++,
            },
        })
        await clickAddBtnMock(wrapper)
        expect(handleAddNewWkeCallCount).to.be.equals(1)
    })
})

describe('PageToolbar - maximize/minimize button tests', () => {
    const is_fullscreen_values = [true, false]
    is_fullscreen_values.forEach(v => {
        it(`Should call SET_FULLSCREEN action to ${
            v ? 'maximize' : 'minimize'
        } page content`, () => {
            let wrapper = mountFactory()
            const btn = wrapper.find('.min-max-btn')
            btn.trigger('click')
            expect(wrapper.vm.is_fullscreen).to.be.equals(v)
        })
    })
})

describe('PageToolbar - query setting button tests', () => {
    it(`Should popup query setting dialog`, () => {
        let wrapper = mountFactory()
        expect(wrapper.vm.queryConfigDialog).to.be.false
        wrapper.find('.query-setting-btn').trigger('click')
        expect(wrapper.vm.queryConfigDialog).to.be.true
    })
})
