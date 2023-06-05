/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
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
                },
                computed: {
                    allQueryTabs: () => dummy_query_tabs,
                    activeQueryTabId: () => dummy_query_tabs[0].id,
                },
                stubs: {
                    'mxs-sql-editor': "<div class='stub'></div>",
                    'txt-editor-toolbar-ctr': "<div class='stub'></div>",
                },
            },
            opts
        )
    )
describe('query-editor', () => {
    describe(`Created hook and child component's data communication tests`, () => {
        let wrapper
        it(`Should pass accurate data to mxs-split-pane via props`, () => {
            wrapper = mountFactory()
            const {
                value,
                boundary,
                minPercent,
                maxPercent,
                split,
                progress,
                revertRender,
            } = wrapper.findComponent({
                name: 'mxs-split-pane',
            }).vm.$props
            expect(value).to.be.equals(wrapper.vm.sidebarPct)
            expect(boundary).to.be.equals(wrapper.vm.$props.ctrDim.width)
            expect(minPercent).to.be.equals(wrapper.vm.minSidebarPct)
            expect(maxPercent).to.be.equals(wrapper.vm.maxSidebarPct)
            expect(split).to.be.equals('vert')
            expect(progress).to.be.equals(true)
            expect(revertRender).to.be.equals(true)
        })

        const fnEvtMap = {
            placeToEditor: 'place-to-editor',
            draggingTxt: 'on-dragging',
            dropTxtToEditor: 'on-dragend',
        }
        Object.keys(fnEvtMap).forEach(key => {
            it(`Should call ${key} if ${fnEvtMap[key]} event is emitted from sidebar-ctr`, () => {
                wrapper = mountFactory({ shallow: false, computed: { isTxtEditor: () => true } })
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
                spyFn.restore()
            })
        })
    })
    const editorModes = ['TXT_EDITOR', 'DDL_EDITOR']
    editorModes.forEach(mode => {
        let wrapper
        describe(`${mode} mode: child component's data communication tests`, () => {
            beforeEach(() => {
                wrapper = mountFactory({
                    computed: { isTxtEditor: () => (mode === 'TXT_EDITOR' ? true : false) },
                })
            })
            const renCom = mode === 'TXT_EDITOR' ? 'txt-editor-ctr' : 'mxs-ddl-editor'
            const hiddenCom = mode === 'TXT_EDITOR' ? 'mxs-ddl-editor' : 'txt-editor-ctr'
            it(`Should render ${renCom}`, () => {
                const editor = wrapper.findAllComponents({ name: renCom }).at(0)
                expect(editor.exists()).to.be.true
            })
            it(`Should pass accurate data to ${renCom} via props`, () => {
                const { dim } = wrapper.findAllComponents({ name: renCom }).at(0).vm.$props
                expect(dim).to.be.deep.equals(wrapper.vm.editorDim)
            })
            it(`Should not render ${hiddenCom}`, () => {
                expect(wrapper.findAllComponents({ name: hiddenCom }).length).to.be.equals(0)
            })
        })
    })
    describe(`Computed, method and other tests`, () => {
        let wrapper
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
