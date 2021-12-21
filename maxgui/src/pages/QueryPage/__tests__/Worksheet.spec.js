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

import mount from '@tests/unit/setup'
import Worksheet from '@/pages/QueryPage/Worksheet'

const dummyCtrDim = { width: 1280, height: 800 }
const mountFactory = opts =>
    mount({
        shallow: false,
        component: Worksheet,
        propsData: {
            ctrDim: dummyCtrDim,
        },
        stubs: {
            'query-editor': "<div class='stub'></div>",
        },
        ...opts,
    })

describe(`Worksheet - created hook and child component's data communication tests`, () => {
    let wrapper
    it(`Should call handleSetSidebarPct on created hook`, () => {
        const handleSetSidebarPctSpy = sinon.spy(Worksheet.methods, 'handleSetSidebarPct')
        wrapper = mountFactory()
        handleSetSidebarPctSpy.should.have.been.calledOnce
        handleSetSidebarPctSpy.restore()
    })
    it(`Should pass accurate data to split-pane via props`, () => {
        wrapper = mountFactory()
        const { value, minPercent, split, disable } = wrapper.findComponent({
            name: 'split-pane',
        }).vm.$props
        expect(value).to.be.equals(wrapper.vm.$data.sidebarPct)
        expect(minPercent).to.be.equals(wrapper.vm.minSidebarPct)
        expect(split).to.be.equals('vert')
        expect(disable).to.be.equals(wrapper.vm.is_sidebar_collapsed)
    })
    const fnEvtMap = {
        placeToEditor: 'place-to-editor',
        draggingTxt: 'on-dragging',
        dropTxtToEditor: 'on-dragend',
    }
    Object.keys(fnEvtMap).forEach(key => {
        it(`Should call ${key} if ${fnEvtMap[key]} event is emitted from sidebar-container`, () => {
            wrapper = mountFactory({
                computed: { isTxtEditor: () => true },
            })
            const spyFn = sinon.spy(wrapper.vm.$refs.txtEditor, key)
            const sidebar = wrapper.findComponent({ name: 'sidebar-container' })
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
    describe(`Worksheet - ${mode} mode: child component's data communication tests`, () => {
        beforeEach(() => {
            wrapper = mountFactory({
                computed: { isTxtEditor: () => (mode === 'TXT_EDITOR' ? true : false) },
            })
        })
        const renCom = mode === 'TXT_EDITOR' ? 'txt-editor-container' : 'ddl-editor-container'
        const hiddenCom = mode === 'TXT_EDITOR' ? 'ddl-editor-container' : 'txt-editor-container'
        it(`Should render ${renCom}`, () => {
            const editor = wrapper.findComponent({ name: renCom })
            expect(editor.exists()).to.be.true
        })
        it(`Should pass accurate data to ${renCom} via props`, () => {
            const { dim } = wrapper.findComponent({ name: renCom }).vm.$props
            expect(dim).to.be.deep.equals(
                wrapper.vm.$data[mode === '' ? 'txtEditorPaneDim' : 'ddlEditorDim']
            )
        })
        it(`Should not render ${hiddenCom}`, () => {
            const editor = wrapper.findComponent({ name: hiddenCom })
            expect(editor.exists()).to.be.false
        })
    })
})
describe(`Worksheet - computed, method and other tests`, () => {
    let wrapper
    it(`Should return accurate value for isTxtEditor`, () => {
        wrapper = mountFactory({
            computed: { getCurrEditorMode: () => 'TXT_EDITOR' },
        })
        expect(wrapper.vm.isTxtEditor).to.be.true
        wrapper = mountFactory({
            computed: { getCurrEditorMode: () => 'DDL_EDITOR' },
        })
        expect(wrapper.vm.isTxtEditor).to.be.false
    })
    const is_sidebar_collapsed_values = [false, true]
    is_sidebar_collapsed_values.forEach(v => {
        describe(`When sidebar is${v ? '' : ' not'} collapsed`, () => {
            it(`Should return accurate value for minSidebarPct`, () => {
                wrapper = mountFactory({ computed: { is_sidebar_collapsed: () => v } })
                const minValueInPx = v ? 40 : 200
                const minPct = (minValueInPx / dummyCtrDim.width) * 100
                expect(wrapper.vm.minSidebarPct).to.be.equals(minPct)
            })
            it(`Should assign accurate value for sidebarPct`, () => {
                wrapper = mountFactory({ computed: { is_sidebar_collapsed: () => v } })
                const defValue = 240
                wrapper.vm.handleSetSidebarPct()
                // if is_sidebar_collapsed is true, use minSidebarPct value
                const sidebarPct = v
                    ? wrapper.vm.minSidebarPct
                    : (defValue / dummyCtrDim.width) * 100
                expect(wrapper.vm.sidebarPct).to.be.equals(sidebarPct)
            })
        })
    })
})
