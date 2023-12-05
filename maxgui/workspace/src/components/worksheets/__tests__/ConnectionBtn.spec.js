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
import ConnectionBtn from '@wkeComps/ConnectionBtn.vue'
import { lodash } from '@share/utils/helpers'

const activeConnStub = {
    id: '923a1239-d8fb-4991-b8f7-3ca3201c12f4',
    active_db: '',
    attributes: { seconds_idle: 0.000401438, sql: null, target: 'server_0', thread_id: 248086 },
    binding_type: 'QUERY_EDITOR',
    meta: { name: 'server_0' },
    clone_of_conn_id: null,
    is_busy: false,
    lost_cnn_err: {},
    erd_task_id: null,
    etl_task_id: null,
    query_tab_id: null,
    query_editor_id: 'c8fd5ff0-4c7f-11ee-a443-675bed5a827c',
}

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: ConnectionBtn,
                propsData: { activeConn: activeConnStub },
            },
            opts
        )
    )

describe('ConnectionBtn', () => {
    let wrapper
    it(`Should pass accurate data to mxs-tooltip-btn`, () => {
        wrapper = mountFactory({ attrs: { btnClass: 'test-btn', depressed: true } })
        const {
            $props: { btnClass, tooltipProps },
            $attrs: { depressed },
        } = wrapper.findComponent({ name: 'mxs-tooltip-btn' }).vm
        expect(btnClass).to.equal('test-btn')
        expect(tooltipProps).to.eql({ disabled: !wrapper.vm.connectedServerName })
        expect(depressed).to.be.true
    })

    it(`Should return an accurate string for connectedServerName`, () => {
        wrapper = mountFactory()
        expect(wrapper.vm.connectedServerName).to.equal(activeConnStub.meta.name)
    })

    it(`Should return a "Connect" string for btnTxt when connectedServerName returns
        an empty string`, () => {
        wrapper = mountFactory({ computed: { connectedServerName: () => '' } })
        expect(wrapper.vm.btnTxt).to.equal(wrapper.vm.$mxs_t('connect'))
    })

    it(`Should disable the tooltip when connectedServerName is an empty string`, () => {
        wrapper = mountFactory({ computed: { connectedServerName: () => '' } })
        const { tooltipProps } = wrapper.findComponent({ name: 'mxs-tooltip-btn' }).vm.$props
        expect(tooltipProps).to.eql({ disabled: true })
    })
})
