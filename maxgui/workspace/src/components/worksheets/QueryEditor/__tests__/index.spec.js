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
import QueryEditor from '@wkeComps/QueryEditor'
import { lodash } from '@share/utils/helpers'

const dummyCtrDim = { width: 1280, height: 800 }
const dummy_query_tabs = [{ id: 'QUERY_TAB_123_45' }]
const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: QueryEditor,
                propsData: {
                    ctrDim: dummyCtrDim,
                    queryEditorId: 'some id',
                },
                computed: {
                    queryTabs: () => dummy_query_tabs,
                    activeQueryTabId: () => dummy_query_tabs[0].id,
                },
                stubs: {
                    'mxs-sql-editor': "<div class='stub'></div>",
                    'txt-editor-toolbar-ctr': "<div class='stub'></div>",
                },
                data: () => ({ isInitializing: false }),
            },
            opts
        )
    )
describe('query-editor', () => {
    let wrapper

    describe(`Created hook and child component's data communication tests`, () => {
        afterEach(() => sinon.restore())

        it(`Should pass accurate data to mxs-split-pane via props`, () => {
            wrapper = mountFactory()
            const {
                value,
                boundary,
                minPercent,
                maxPercent,
                split,
                progress,
            } = wrapper.findComponent({
                name: 'mxs-split-pane',
            }).vm.$props
            expect(value).to.be.equals(wrapper.vm.sidebarPct)
            expect(boundary).to.be.equals(wrapper.vm.$props.ctrDim.width)
            expect(minPercent).to.be.equals(wrapper.vm.minSidebarPct)
            expect(maxPercent).to.be.equals(wrapper.vm.maxSidebarPct)
            expect(split).to.be.equals('vert')
            expect(progress).to.be.equals(true)
        })

        it(`Should pass accurate data to sidebar-ctr via props`, () => {
            wrapper = mountFactory()
            const {
                queryEditorId,
                queryEditorTmp,
                activeQueryTabId,
                activeQueryTabConn,
            } = wrapper.findComponent({
                name: 'sidebar-ctr',
            }).vm.$props
            expect(queryEditorId).to.equal(wrapper.vm.$props.queryEditorId)
            expect(queryEditorTmp).to.eql(wrapper.vm.queryEditorTmp)
            expect(activeQueryTabId).to.equal(wrapper.vm.activeQueryTabId)
            expect(activeQueryTabConn).to.eql(wrapper.vm.activeQueryTabConn)
        })

        it(`Should pass accurate data to query-tab-nav-ctr via props`, () => {
            wrapper = mountFactory()
            const {
                queryEditorId,
                activeQueryTabId,
                activeQueryTabConn,
                queryTabs,
                height,
            } = wrapper.findComponent({
                name: 'query-tab-nav-ctr',
            }).vm.$props
            expect(queryEditorId).to.equal(wrapper.vm.$props.queryEditorId)
            expect(queryTabs).to.eql(wrapper.vm.queryTabs)
            expect(activeQueryTabId).to.equal(wrapper.vm.activeQueryTabId)
            expect(activeQueryTabConn).to.eql(wrapper.vm.activeQueryTabConn)
            expect(height).to.equal(wrapper.vm.$data.queryTabCtrHeight)
        })

        const fnEvtMap = {
            placeToEditor: 'place-to-editor',
            draggingTxt: 'on-dragging',
            dropTxtToEditor: 'on-dragend',
        }
        Object.keys(fnEvtMap).forEach(key => {
            it(`Should call ${key} if ${fnEvtMap[key]} event is emitted from sidebar-ctr`, () => {
                wrapper = mountFactory({ shallow: false, computed: { isSqlEditor: () => true } })
                const spyFn = sinon.spy(
                    wrapper.vm.$typy(wrapper.vm.$refs, 'editor[0]').safeObject,
                    key
                )
                const sidebar = wrapper.findComponent({ name: 'sidebar-ctr' })
                let param
                switch (key) {
                    case 'placeToEditor':
                        param = '`test`'
                        break
                    case 'draggingTxt':
                    case 'dropTxtToEditor':
                        param = 'event'
                        break
                }
                sidebar.vm.$emit(fnEvtMap[key], param)
                spyFn.should.have.been.calledOnceWith(param)
            })
        })
    })
    const queryTabTypes = ['SQL_EDITOR', 'ALTER_EDITOR', 'INSIGHT_VIEWER']
    queryTabTypes.forEach(type => {
        describe(`${type} type: child component's data communication tests`, () => {
            beforeEach(() => {
                wrapper = mountFactory({
                    computed: {
                        isSqlEditor: () => (type === 'SQL_EDITOR' ? true : false),
                        isAlterEditor: () => (type === 'ALTER_EDITOR' ? true : false),
                    },
                })
            })
            let renComp = '',
                hiddenComps = []
            switch (type) {
                case 'ALTER_EDITOR':
                    renComp = 'alter-table-editor'
                    hiddenComps = ['txt-editor-ctr', 'insight-viewer']
                    break
                case 'INSIGHT_VIEWER':
                    renComp = 'insight-viewer'
                    hiddenComps = ['txt-editor-ctr', 'alter-table-editor']
                    break
                case 'SQL_EDITOR':
                    renComp = 'txt-editor-ctr'
                    hiddenComps = ['insight-viewer', 'alter-table-editor']
                    break
            }
            it(`Should render ${renComp}`, () => {
                const editor = wrapper.findAllComponents({ name: renComp }).at(0)
                expect(editor.exists()).to.be.true
            })
            it(`Should pass accurate data to ${renComp} via props`, () => {
                const { dim } = wrapper.findAllComponents({ name: renComp }).at(0).vm.$props
                expect(dim).to.be.deep.equals(wrapper.vm.editorDim)
            })
            hiddenComps.forEach(hiddenCom =>
                it(`Should not render ${hiddenCom}`, () => {
                    expect(wrapper.findAllComponents({ name: hiddenCom }).length).to.be.equals(0)
                })
            )
        })
    })
    describe(`Computed, method and other tests`, () => {
        const is_sidebar_collapsed_values = [false, true]
        is_sidebar_collapsed_values.forEach(v => {
            describe(`When sidebar is${v ? '' : ' not'} collapsed`, () => {
                it(`Should return accurate value for sidebarPct`, () => {
                    wrapper = mountFactory({ computed: { is_sidebar_collapsed: () => v } })
                    // if is_sidebar_collapsed is true, use minSidebarPct value
                    const sidebarPct = v ? wrapper.vm.minSidebarPct : wrapper.vm.defSidebarPct

                    expect(wrapper.vm.sidebarPct).to.be.equals(sidebarPct)
                })
            })
        })
    })
})
