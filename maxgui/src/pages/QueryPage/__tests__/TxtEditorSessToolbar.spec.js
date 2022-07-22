/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import TxtEditorSessToolbar from '@/pages/QueryPage/TxtEditorSessToolbar'

const mountFactory = opts =>
    mount({
        shallow: false,
        component: TxtEditorSessToolbar,
        stubs: {
            'readonly-query-editor': "<div class='stub'></div>",
        },
        ...opts,
    })
const dummy_session_id = 'SESSION_123_45'
describe(`txt-editor-sess-toolbar`, () => {
    let wrapper
    describe(`Child component's data communication tests`, () => {
        it(`Should pass accurate data to confirm-dialog`, () => {
            // shallow mount so that confirm-dialog in connection-manager will be stubbed
            wrapper = mountFactory({ shallow: true, computed: { query_confirm_flag: () => 1 } })
            const confirmDialog = wrapper.findComponent({ name: 'confirm-dialog' })
            const { value, title, onSave, closeImmediate } = confirmDialog.vm.$attrs
            const { type } = confirmDialog.vm.$props
            expect(value).to.be.equals(wrapper.vm.confDlg.isOpened)
            expect(title).to.be.equals(wrapper.vm.confDlg.title)
            expect(type).to.be.equals(wrapper.vm.confDlg.type)
            expect(onSave).to.be.equals(wrapper.vm.confDlg.onSave)
            expect(closeImmediate).to.be.true
        })
        it(`Should render max-rows-ctr`, () => {
            wrapper = mountFactory({ shallow: true })
            const input = wrapper.findComponent({ name: 'max-rows-ctr' })
            expect(input.exists()).to.be.true
        })
        it(`Should call SET_QUERY_MAX_ROW when @change event is emitted
        from max-rows-ctr`, () => {
            let callCount = 0,
                arg
            wrapper = mountFactory({
                shallow: true,
                methods: {
                    SET_QUERY_MAX_ROW: val => {
                        callCount++
                        arg = val
                    },
                },
            })
            const newVal = 123
            wrapper.findComponent({ name: 'max-rows-ctr' }).vm.$emit('change', newVal)
            expect(callCount).to.be.equals(1)
            expect(arg).to.be.equals(newVal)
        })
    })
    describe('Save to snippets tests', () => {
        let wrapper
        it(`Should disable save to snippets button if query_txt is empty `, () => {
            wrapper = mountFactory({ computed: { isTxtEditor: () => true } })
            const saveToSnippetsBtn = wrapper.find('.create-snippet-btn')
            expect(saveToSnippetsBtn.element.disabled).to.be.true
        })
        it(`Should allow query to be saved to snippets if query_txt is not empty`, () => {
            wrapper = mountFactory({ computed: { query_txt: () => 'SELECT 1' } })
            const saveToSnippetsBtn = wrapper.find('.create-snippet-btn')
            expect(saveToSnippetsBtn.element.disabled).to.be.false
        })
        it(`Should popup dialog to save query text to snippets`, () => {
            expect(wrapper.vm.confDlg.isOpened).to.be.false
            wrapper = mountFactory({ computed: { query_txt: () => 'SELECT 1' } })
            wrapper.find('.create-snippet-btn').trigger('click')
            expect(wrapper.vm.confDlg.isOpened).to.be.true
        })
        it(`Should generate snippet object before popup the dialog`, () => {
            wrapper = mountFactory({ computed: { query_txt: () => 'SELECT 1' } })
            wrapper.find('.create-snippet-btn').trigger('click')
            expect(wrapper.vm.snippet.name).to.be.equals('')
        })
        it(`Should call addSnippet`, async () => {
            wrapper = mountFactory({
                computed: { query_txt: () => 'SELECT 1', isTxtEditor: () => true },
            })
            const addToSnippetsSpy = sinon.spy(wrapper.vm, 'addSnippet')
            wrapper.vm.openSnippetDlg()
            await wrapper
                .findComponent({ name: 'confirm-dialog' })
                .find('.save')
                .trigger('click')
            await wrapper.vm.$help.delay(300)
            addToSnippetsSpy.should.have.been.calledOnce
        })
    })
    describe('session-btns event handlers', () => {
        let sessionBtns
        beforeEach(() => {
            wrapper = mountFactory({ computed: { getActiveSessionId: () => dummy_session_id } })
            sessionBtns = wrapper.findComponent({ name: 'session-btns' })
        })
        it(`Should call SET_SHOW_VIS_SIDEBAR action`, () => {
            const payload = !wrapper.vm.show_vis_sidebar
            const spy = sinon.spy(wrapper.vm, 'SET_SHOW_VIS_SIDEBAR')
            sessionBtns.vm.$emit('on-visualize')
            spy.should.have.been.calledOnceWithExactly({ payload, id: dummy_session_id })
        })
        it(`Should call handleRun method`, () => {
            const spy = sinon.spy(wrapper.vm, 'handleRun')
            sessionBtns.vm.$emit('on-run')
            spy.should.have.been.calledOnceWithExactly('all')
        })
        it(`Should call stopQuery action`, () => {
            const spy = sinon.spy(TxtEditorSessToolbar.methods, 'stopQuery')
            wrapper = mountFactory()
            wrapper.findComponent({ name: 'session-btns' }).vm.$emit('on-stop-query')
            spy.should.have.been.calledOnce
        })
    })
    describe('session-btns - Run query tests', () => {
        it(`Should popup query confirmation dialog with accurate data
      when query_confirm_flag = 1`, async () => {
            wrapper = mountFactory({
                computed: {
                    query_txt: () => 'SELECT 1',
                    query_confirm_flag: () => 1,
                    getActiveSessionId: () => dummy_session_id,
                    getShouldDisableExecuteMap: () => ({ [dummy_session_id]: false }),
                },
            })
            sinon.stub(wrapper.vm, 'onRun')
            await wrapper.vm.handleRun('all')
            expect(wrapper.vm.activeRunMode).to.be.equals('all')
            expect(wrapper.vm.dontShowConfirm).to.be.false
            expect(wrapper.vm.confDlg.isOpened).to.be.true
        })
        it(`Should call SET_QUERY_CONFIRM_FLAG action if dontShowConfirm
      checkbox is checked when confirm running query`, async () => {
            let setQueryConfirmFlagCallCount = 0
            wrapper = mountFactory({
                computed: {
                    getShouldDisableExecuteMap: () => ({ [dummy_session_id]: false }),
                    query_confirm_flag: () => 1,
                },
                methods: {
                    SET_QUERY_CONFIRM_FLAG: () => setQueryConfirmFlagCallCount++,
                },
            })
            sinon.stub(wrapper.vm, 'fetchQueryResult')
            sinon.stub(wrapper.vm, 'SET_CURR_QUERY_MODE')
            await wrapper.setData({
                dontShowConfirm: true,
                isConfDlgOpened: true,
                activeRunMode: 'all',
            })
            await wrapper.vm.confirmRunning()
            expect(setQueryConfirmFlagCallCount).to.be.equals(1)
        })
    })
})
