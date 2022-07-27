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
import Worksheets from '@/pages/QueryPage/Worksheets'

const mountFactory = opts =>
    mount({
        shallow: false,
        component: Worksheets,
        propsData: {
            ctrDim: { width: 1280, height: 800 },
        },
        stubs: {
            'query-editor': "<div class='stub'></div>",
            'readonly-query-editor': "<div class='stub'></div>",
        },
        ...opts,
    })

describe('Worksheets', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mountFactory()
    })

    it('Should pass accurate data to wke-ctr component via props', () => {
        wrapper = mountFactory({
            computed: {
                active_wke_id: () => wrapper.vm.worksheets_arr[0].id,
            },
        })
        const wke = wrapper.findComponent({ name: 'wke-ctr' })
        expect(wke.vm.$props.ctrDim).to.be.equals(wrapper.vm.$props.ctrDim)
    })

    it('Should not show delete worksheet button when worksheets_arr length <= 1', () => {
        expect(wrapper.vm.worksheets_arr.length).to.be.equals(1)
        expect(wrapper.find('.del-wke-btn').exists()).to.be.equal(false)
    })

    it('Should show delete worksheet button when worksheets_arr length > 1', () => {
        expect(wrapper.vm.worksheets_arr.length).to.be.equals(1)
        // stubs worksheets_arr
        wrapper = mountFactory({
            computed: {
                worksheets_arr: () => [
                    ...wrapper.vm.worksheets_arr,
                    { ...wrapper.vm.worksheets_arr[0], id: 'dummy_1234' },
                ],
            },
        })
        expect(wrapper.vm.worksheets_arr.length).to.be.equals(2)
        expect(wrapper.find('.del-wke-btn').exists()).to.be.equal(true)
    })

    it('Should not show a tooltip when hovering a worksheet tab has no connection', () => {
        expect(wrapper.findComponent({ name: 'v-tooltip' }).vm.$props.disabled).to.be.true
    })

    it('Should show a tooltip when hovering a worksheet tab has a connection', () => {
        wrapper = mountFactory({
            computed: {
                getWkeFirstSessConnByWkeId: () => () => ({
                    id: '0',
                    name: 'server_0',
                    type: 'servers',
                }),
            },
        })
        expect(wrapper.findComponent({ name: 'v-tooltip' }).vm.$props.disabled).to.be.false
    })
})

describe('Should assign corresponding handler for worksheet shortcut keys accurately', () => {
    let wrapper, sessionToolbar, wke, handleRunSpy, openSnippetDlgSpy, handleFileOpenSpy
    beforeEach(() => {
        wrapper = mountFactory({
            computed: {
                supportFs: () => false,
            },
        })
        sessionToolbar = wrapper.vm.$refs.wke.$refs.sessionToolbar
        wke = wrapper.findComponent({ name: 'wke-ctr' })
        handleRunSpy = sinon.spy(sessionToolbar, 'handleRun')
        openSnippetDlgSpy = sinon.spy(sessionToolbar, 'openSnippetDlg')
        handleFileOpenSpy = sinon.spy(sessionToolbar.$refs.loadSql, 'handleFileOpen')
    })

    afterEach(() => {
        handleRunSpy.restore()
        openSnippetDlgSpy.restore()
        handleFileOpenSpy.restore()
    })

    it('Handle onCtrlEnter evt', () => {
        wke.vm.$emit('onCtrlEnter')
        handleRunSpy.should.have.been.calledOnce
        handleRunSpy.should.have.been.calledWith('selected')
    })
    it('Handle onCtrlShiftEnter evt', () => {
        wke.vm.$emit('onCtrlShiftEnter')
        handleRunSpy.should.have.been.calledOnce
        handleRunSpy.should.have.been.calledWith('all')
    })
    it('Handle onCtrlD evt', () => {
        wke.vm.$emit('onCtrlD')
        openSnippetDlgSpy.should.have.been.calledOnce
    })
    it('Handle onCtrlO evt', () => {
        wke.vm.$emit('onCtrlO')
        handleFileOpenSpy.should.have.been.calledOnce
    })
})
