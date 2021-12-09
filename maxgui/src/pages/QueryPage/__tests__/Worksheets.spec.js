/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import Worksheets from '@/pages/QueryPage/Worksheets'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'

chai.should()
chai.use(sinonChai)

function getCurrActiveWkeId({ wrapper, idx }) {
    return wrapper.vm.worksheets_arr[idx].id
}
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

/**
 * stubs worksheet object in worksheets_arr with a valid connection
 * @param {Object} wrapper - A Wrapper is an object that contains a mounted component and methods to test the component
 * @return {Object} - return new wrapper
 */
function stubCnctWke(wrapper) {
    return mountFactory({
        computed: {
            worksheets_arr: () => [
                {
                    ...wrapper.vm.worksheets_arr[0],
                    curr_cnct_resource: {
                        id: '0',
                        name: 'server_0',
                        type: 'servers',
                    },
                },
            ],
        },
    })
}
describe('Worksheets', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mountFactory()
        // mount again to getCurrActiveWkeId
        wrapper = mountFactory({
            computed: {
                active_wke_id: () => getCurrActiveWkeId({ wrapper, idx: 0 }),
            },
        })
    })

    it('Should pass accurate data to worksheet component via props', () => {
        const wke = wrapper.findComponent({ name: 'worksheet' })
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
        wrapper = stubCnctWke(wrapper)
        expect(wrapper.findComponent({ name: 'v-tooltip' }).vm.$props.disabled).to.be.false
    })

    describe('Should assign corresponding handler for worksheet shortcut keys accurately', () => {
        let wkeToolbar, pageToolbar, wke, handleRunSpy, openFavoriteDialogSpy
        beforeEach(() => {
            wrapper = mountFactory()
            wkeToolbar = wrapper.vm.$refs.wkeToolbar
            pageToolbar = wrapper.vm.$refs.pageToolbar
            wke = wrapper.findComponent({ name: 'worksheet' })
            handleRunSpy = sinon.spy(wkeToolbar, 'handleRun')
            openFavoriteDialogSpy = sinon.spy(pageToolbar, 'openFavoriteDialog')
        })

        afterEach(() => {
            handleRunSpy.restore()
            openFavoriteDialogSpy.restore()
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
        it('Handle onCtrlS evt', () => {
            wke.vm.$emit('onCtrlS')
            openFavoriteDialogSpy.should.have.been.calledOnce
        })
    })
})
