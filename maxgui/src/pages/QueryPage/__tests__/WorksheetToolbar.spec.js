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
import WorksheetToolbar from '@/pages/QueryPage/WorksheetToolbar'

const mountFactory = opts =>
    mount({
        shallow: false,
        component: WorksheetToolbar,
        stubs: {
            'readonly-query-editor': "<div class='stub'></div>",
        },
        ...opts,
    })

describe(`WorksheetToolbar - child component's data communication tests`, () => {
    let wrapper
    it('Should pass accurate data to connection-manager', () => {
        wrapper = mountFactory()
        const cnnMan = wrapper.findComponent({ name: 'connection-manager' })
        expect(cnnMan.vm.$props.disabled).to.be.equals(wrapper.vm.getIsQuerying)
    })
    it(`Should pass accurate data to confirm running query dialog via props`, () => {
        // shallow mount so that confirm-dialog in connection-manager will be stubbed
        wrapper = mountFactory({ shallow: true, computed: { query_confirm_flag: () => 1 } })
        const confirmDialog = wrapper.findComponent({ name: 'confirm-dialog' })
        const { value, title, type, onSave, closeImmediate } = confirmDialog.vm.$props
        expect(value).to.be.equals(wrapper.vm.isConfDlgOpened)
        expect(title).to.be.equals(wrapper.vm.$t('confirmations.runQuery'))
        expect(type).to.be.equals('run')
        expect(onSave).to.be.equals(wrapper.vm.confirmRunning)
        expect(closeImmediate).to.be.true
    })
    it(`Should not render confirm running query dialog if query_confirm_flag = 0`, () => {
        // shallow mount so that confirm-dialog in connection-manager will be stubbed
        wrapper = mountFactory({ shallow: true, computed: { query_confirm_flag: () => 0 } })
        expect(wrapper.findComponent({ name: 'confirm-dialog' }).exists()).to.be.false
    })
})

describe('WorksheetToolbar - use-db-btn, run-btn and visualize-btn common tests', () => {
    let wrapper
    const btns = ['use-db-btn', 'run-btn', 'visualize-btn']
    btns.forEach(btn => {
        it(`Should disable ${btn} if there is a running query`, () => {
            wrapper = mountFactory({
                computed: {
                    query_txt: () => 'SELECT 1',
                    hasActiveConn: () => true,
                    getIsQuerying: () => true,
                    getLoadingQueryResult: () => false,
                },
            })
            const btnComponent = wrapper.find(`.${btn}`)
            expect(btnComponent.element.disabled).to.be.true
        })
    })
    btns.forEach(btn => {
        it(`Should disable ${btn} if there is no connected connection to current worksheet`, () => {
            wrapper = mountFactory({
                computed: {
                    query_txt: () => 'SELECT 1',
                    hasActiveConn: () => false,
                    getIsQuerying: () => false,
                    getLoadingQueryResult: () => false,
                },
            })
            const useDbBtn = wrapper.find('.use-db-btn')
            expect(useDbBtn.element.disabled).to.be.true
        })
    })
    btns.forEach(btn => {
        it(`${btn} should be clickable if it matches certain conditions`, () => {
            wrapper = mountFactory({
                computed: {
                    query_txt: () => 'SELECT 1',
                    hasActiveConn: () => true,
                    getIsQuerying: () => false,
                    getLoadingQueryResult: () => false,
                },
            })
            const btnComponent = wrapper.find(`.${btn}`)
            expect(btnComponent.element.disabled).to.be.false
        })
    })
})

describe('WorksheetToolbar - Use database button tests', () => {
    let wrapper

    it(`Should show 'Use database' if there is no active database`, () => {
        wrapper = mountFactory({ computed: { active_db: () => '' } })
        const useDbBtn = wrapper.find('.use-db-btn')
        expect(useDbBtn.text()).to.be.equals(wrapper.vm.$t('useDb'))
    })
    it(`Should render accurate active db name`, () => {
        const dummy_active_db = 'test'
        wrapper = mountFactory({
            computed: {
                active_db: () => dummy_active_db,
                hasActiveConn: () => true,
            },
        })
        const useDbBtn = wrapper.find('.use-db-btn')
        expect(useDbBtn.text()).to.be.equals(dummy_active_db)
    })
    //TODO: Add test for changing active db dbListMenu
})

describe('WorksheetToolbar - Run button tests', () => {
    let wrapper
    it(`Should disable the button if query_txt is empty`, () => {
        wrapper = mountFactory({ computed: { query_txt: () => '' } })
        const btn = wrapper.find('.run-btn')
        expect(btn.element.disabled).to.be.true
    })
    it(`Should not disable run-btn if query result is still loading`, () => {
        wrapper = mountFactory({
            computed: {
                query_txt: () => 'SELECT 1',
                hasActiveConn: () => true,
                getIsQuerying: () => true,
                getLoadingQueryResult: () => true,
            },
        })
        const btnComponent = wrapper.find(`.run-btn`)
        expect(btnComponent.element.disabled).to.be.false
    })
    const runModes = ['all', 'selected']
    const getConditionsToRun = (mode = 'all') => ({
        query_txt: () => 'SELECT 1; SELECT 2;',
        selected_query_txt: () => (mode === 'selected' ? 'SELECT 2;' : ''),
        hasActiveConn: () => true,
        getIsQuerying: () => false,
        getLoadingQueryResult: () => false,
    })
    runModes.forEach(mode => {
        it(`Should call handleRun with accurate mode '${mode}'`, () => {
            wrapper = mountFactory({ computed: getConditionsToRun(mode) })
            const handleRunSpy = sinon.spy(wrapper.vm, 'handleRun')
            sinon.stub(wrapper.vm, 'onRun') // stub onRun
            const btn = wrapper.find('.run-btn')
            btn.trigger('click')
            handleRunSpy.should.have.been.calledOnceWith(mode)
        })
    })
    runModes.forEach(mode => {
        it(`Should call onRun with mode '${mode}' when query_confirm_flag = 0`, () => {
            wrapper = mountFactory({
                computed: { ...getConditionsToRun(mode), query_confirm_flag: () => 0 },
            })
            const onRunSpy = sinon.spy(wrapper.vm, 'onRun')
            sinon.stub(wrapper.vm, 'fetchQueryResult')
            sinon.stub(wrapper.vm, 'SET_CURR_QUERY_MODE')
            const btn = wrapper.find('.run-btn')
            btn.trigger('click')
            onRunSpy.should.have.been.calledOnceWith(mode)
        })
    })
    it(`Should popup query confirmation dialog with accurate data
      when query_confirm_flag = 1`, () => {
        wrapper = mountFactory({
            computed: {
                ...getConditionsToRun(),
                query_confirm_flag: () => 1,
            },
        })
        const onRunSpy = sinon.spy(wrapper.vm, 'onRun')
        sinon.stub(wrapper.vm, 'fetchQueryResult')
        sinon.stub(wrapper.vm, 'SET_CURR_QUERY_MODE')
        const btn = wrapper.find('.run-btn')
        btn.trigger('click')
        onRunSpy.should.have.not.been.called
        expect(wrapper.vm.activeRunMode).to.be.equals('all')
        expect(wrapper.vm.dontShowConfirm).to.be.false
        expect(wrapper.vm.isConfDlgOpened).to.be.true
    })
    it(`Should call SET_QUERY_CONFIRM_FLAG action if dontShowConfirm
      checkbox is checked when confirm running query`, async () => {
        let setQueryConfirmFlagCallCount = 0
        wrapper = mountFactory({
            computed: { ...getConditionsToRun(), query_confirm_flag: () => 1 },
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

describe('WorksheetToolbar - visualize-btn tests', () => {
    let wrapper
    it(`Should call SET_SHOW_VIS_SIDEBAR action with accurate argument`, () => {
        let setShowVisSidebarCallCount = 0
        let argVal
        wrapper = mountFactory({
            computed: { hasActiveConn: () => true, getIsQuerying: () => false },
            methods: {
                SET_SHOW_VIS_SIDEBAR: val => {
                    setShowVisSidebarCallCount++
                    argVal = val
                },
            },
        })
        wrapper.find('.visualize-btn').trigger('click')
        expect(setShowVisSidebarCallCount).to.be.equals(1)
        expect(argVal).to.be.equals(!wrapper.vm.show_vis_sidebar)
    })
})
