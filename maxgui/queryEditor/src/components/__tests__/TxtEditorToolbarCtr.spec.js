/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import TxtEditorToolbarCtr from '../TxtEditorToolbarCtr.vue'
import { lodash } from '@share/utils/helpers'

const dummy_session = { id: 'SESSION_123_45' }
const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: false,
                component: TxtEditorToolbarCtr,
                stubs: {
                    'readonly-sql-editor': "<div class='stub'></div>",
                },
                propsData: {
                    session: dummy_session,
                },
                computed: {
                    getActiveSessionId: () => dummy_session.id,
                },
            },
            opts
        )
    )
describe(`txt-editor-toolbar-ctr`, () => {
    let wrapper
    describe(`Child component's data communication tests`, () => {
        it(`Should pass accurate data to mxs-conf-dlg`, () => {
            // shallow mount so that mxs-conf-dlg in connection-manager will be stubbed
            wrapper = mountFactory({ shallow: true, computed: { query_confirm_flag: () => 1 } })
            const confirmDialog = wrapper.findComponent({ name: 'mxs-conf-dlg' })
            const { value, title, onSave, closeImmediate, saveText } = confirmDialog.vm.$attrs
            expect(value).to.be.equals(wrapper.vm.confDlg.isOpened)
            expect(title).to.be.equals(wrapper.vm.confDlg.title)
            expect(saveText).to.be.equals(wrapper.vm.confDlg.type)
            expect(onSave).to.be.equals(wrapper.vm.confDlg.onSave)
            expect(closeImmediate).to.be.true
        })
        it(`Should render row-limit-ctr`, () => {
            wrapper = mountFactory({ shallow: true })
            const input = wrapper.findComponent({ name: 'row-limit-ctr' })
            expect(input.exists()).to.be.true
        })
        it(`Should call SET_QUERY_ROW_LIMIT when @change event is emitted
        from row-limit-ctr`, () => {
            let callCount = 0,
                arg
            wrapper = mountFactory({
                shallow: true,
                methods: {
                    SET_QUERY_ROW_LIMIT: val => {
                        callCount++
                        arg = val
                    },
                },
            })
            const newVal = 123
            wrapper.findComponent({ name: 'row-limit-ctr' }).vm.$emit('change', newVal)
            expect(callCount).to.be.equals(1)
            expect(arg).to.be.equals(newVal)
        })
    })
    describe('Save to snippets tests', () => {
        wrapper
        it(`Should disable save to snippets button if query_txt is empty `, () => {
            wrapper = mountFactory()
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
        it(`Should assign addSnippet as the save handler for confDlg`, () => {
            wrapper = mountFactory({ computed: { query_txt: () => 'SELECT 1' } })
            wrapper.vm.openSnippetDlg()
            expect(wrapper.vm.$data.confDlg.onSave).to.be.equals(wrapper.vm.addSnippet)
        })
    })
    describe('Run button and visualize button tests', () => {
        const btns = ['run-btn', 'visualize-btn']
        btns.forEach(btn => {
            let des = `Should disable ${btn} if there is a running query`,
                btnClassName = `.${btn}`
            if (btn === 'run-btn') {
                des = `Should render 'stop-btn' if there is a running query`
                btnClassName = '.stop-btn'
            }
            it(des, () => {
                wrapper = mountFactory({
                    computed: {
                        getLoadingQueryResultBySessionId: () => () => true,
                        getIsRunBtnDisabledBySessionId: () => () => true,
                        getIsVisBtnDisabledBySessionId: () => () => true,
                    },
                })
                if (btn === 'run-btn') {
                    expect(wrapper.find('.stop-btn').exists()).to.be.equal(true)
                } else {
                    const btnComponent = wrapper.find(btnClassName)
                    expect(btnComponent.element.disabled).to.be.true
                }
            })
            it(`${btn} should be clickable if it matches certain conditions`, () => {
                wrapper = mountFactory({
                    computed: {
                        getLoadingQueryResultBySessionId: () => () => false,
                        getIsRunBtnDisabledBySessionId: () => () => false,
                        getIsVisBtnDisabledBySessionId: () => () => false,
                    },
                })
                const btnComponent = wrapper.find(`.${btn}`)
                expect(btnComponent.element.disabled).to.be.false
            })
            let handler = 'SET_SHOW_VIS_SIDEBAR'
            if (btn === 'run-btn') handler = 'handleRun'
            it(`Should call ${handler}`, () => {
                wrapper = mountFactory({
                    computed: {
                        getLoadingQueryResultBySessionId: () => () => false,
                        getIsRunBtnDisabledBySessionId: () => () => false,
                        getIsVisBtnDisabledBySessionId: () => () => false,
                    },
                })
                const spy = sinon.spy(wrapper.vm, handler)
                const show_vis_sidebar = wrapper.vm.show_vis_sidebar
                wrapper.find(`.${btn}`).trigger('click')
                switch (btn) {
                    case 'visualize-btn':
                        spy.should.have.been.calledOnceWithExactly({
                            payload: !show_vis_sidebar,
                            id: dummy_session.id,
                        })
                        break
                    case 'run-btn':
                        spy.should.have.been.calledOnceWithExactly('all')
                        break
                }
                spy.restore()
            })
        })
    })
    describe('Run query tests', () => {
        it(`Should popup query confirmation dialog with accurate data
      when query_confirm_flag = 1`, async () => {
            wrapper = mountFactory({
                computed: {
                    query_txt: () => 'SELECT 1',
                    query_confirm_flag: () => 1,
                    getIsRunBtnDisabledBySessionId: () => () => false,
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
                    getIsRunBtnDisabledBySessionId: () => () => false,
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
    describe('Stop button tests', () => {
        it(`Should render stop-btn if query result is loading`, () => {
            wrapper = mountFactory({
                computed: {
                    getLoadingQueryResultBySessionId: () => () => true,
                    getIsRunBtnDisabledBySessionId: () => () => false,
                },
            })
            expect(wrapper.find('.stop-btn').exists()).to.be.equal(true)
        })
    })
})
