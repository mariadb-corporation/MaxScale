/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import TxtEditorToolbarCtr from '@wkeComps/QueryEditor/TxtEditorToolbarCtr.vue'
import { lodash } from '@share/utils/helpers'

const dummy_query_tab = { id: 'QUERY_TAB_123_45' }
const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: false,
                component: TxtEditorToolbarCtr,
                stubs: {
                    'mxs-sql-editor': "<div class='stub'></div>",
                },
                propsData: {
                    height: 30,
                    queryTab: dummy_query_tab,
                    queryTabTmp: {},
                    queryTabConn: {},
                    queryTxt: '',
                    isVisSidebarShown: false,
                },
            },
            opts
        )
    )
describe(`txt-editor-toolbar-ctr`, () => {
    let wrapper

    describe(`Child component's data communication tests`, () => {
        it(`Should pass accurate data to mxs-dlg`, () => {
            // shallow mount so that mxs-dlg in connection-manager will be stubbed
            wrapper = mountFactory({ shallow: true, computed: { query_confirm_flag: () => 1 } })
            const { value, title, onSave, closeImmediate, saveText } = wrapper.findComponent({
                name: 'mxs-dlg',
            }).vm.$props
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
        it(`Should disable save to snippets button if queryTxt is empty `, () => {
            wrapper = mountFactory()
            const saveToSnippetsBtn = wrapper.find('.create-snippet-btn')
            expect(saveToSnippetsBtn.element.disabled).to.be.true
        })
        it(`Should allow query to be saved to snippets if queryTxt is not empty`, () => {
            wrapper = mountFactory({ propsData: { queryTxt: 'SELECT 1' } })
            const saveToSnippetsBtn = wrapper.find('.create-snippet-btn')
            expect(saveToSnippetsBtn.element.disabled).to.be.false
        })
        it(`Should popup dialog to save query text to snippets`, () => {
            expect(wrapper.vm.confDlg.isOpened).to.be.false
            wrapper = mountFactory({ propsData: { queryTxt: 'SELECT 1' } })
            wrapper.find('.create-snippet-btn').trigger('click')
            expect(wrapper.vm.confDlg.isOpened).to.be.true
        })
        it(`Should generate snippet object before popup the dialog`, () => {
            wrapper = mountFactory({ propsData: { queryTxt: 'SELECT 1' } })
            wrapper.find('.create-snippet-btn').trigger('click')
            expect(wrapper.vm.snippet.name).to.be.equals('')
        })
        it(`Should assign addSnippet as the save handler for confDlg`, () => {
            wrapper = mountFactory({ propsData: { queryTxt: 'SELECT 1' } })
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
                        isExecuting: () => true,
                        isRunBtnDisabled: () => true,
                        isVisBtnDisabled: () => true,
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
                        isExecuting: () => false,
                        isRunBtnDisabled: () => false,
                        isVisBtnDisabled: () => false,
                    },
                })
                const btnComponent = wrapper.find(`.${btn}`)
                expect(btnComponent.element.disabled).to.be.false
            })

            const handler = btn === 'run-btn' ? 'handleRun' : 'toggleVisSidebar'
            it(`Should call ${handler}`, () => {
                let callCount = 0,
                    param = ''
                wrapper = mountFactory({
                    computed: {
                        isExecuting: () => false,
                        isRunBtnDisabled: () => false,
                        isVisBtnDisabled: () => false,
                    },
                    methods: {
                        [handler]: args => {
                            callCount++
                            param = args
                        },
                    },
                })
                wrapper.find(`.${btn}`).trigger('click')
                expect(callCount).to.be.equal(1)
                if (btn === 'run-btn') expect(param).to.be.equal('all')
            })
        })
    })
    describe('Run query tests', () => {
        afterEach(() => sinon.restore())

        it(`Should popup query confirmation dialog with accurate data
      when query_confirm_flag = 1`, async () => {
            wrapper = mountFactory({
                computed: {
                    queryTxt: () => 'SELECT 1',
                    query_confirm_flag: () => 1,
                    isRunBtnDisabled: () => false,
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
                    isRunBtnDisabled: () => false,
                    query_confirm_flag: () => 1,
                },
                methods: {
                    SET_QUERY_CONFIRM_FLAG: () => setQueryConfirmFlagCallCount++,
                },
            })
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
                    isExecuting: () => true,
                    isRunBtnDisabled: () => false,
                },
            })
            expect(wrapper.find('.stop-btn').exists()).to.be.equal(true)
        })
    })
})
