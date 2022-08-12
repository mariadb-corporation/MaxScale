/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import QueryEditor from '../index.vue'
import { lodash } from '@share/utils/helpers'

const sql_conns_mock = {
    1: {
        id: '1',
        name: 'Read-Write-Listener',
        type: 'listeners',
    },
}
const from_route_mock = { name: 'queryEditor', path: '/query' }
const to_route_mock = { name: 'settings', path: '/settings' }
function mockBeforeRouteLeave(wrapper) {
    const next = sinon.stub()
    wrapper.vm.beforeRouteLeaveHandler(to_route_mock, from_route_mock, next) // stub
}
const stubModuleMethods = {
    handleAutoClearQueryHistory: () => null,
    validatingConn: () => null,
}

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: false,
                component: QueryEditor,
                computed: {
                    is_validating_conn: () => false,
                    worksheets_arr: () => [{ id: 'WORKSHEET_123' }],
                    active_wke_id: () => 'WORKSHEET_123',
                    ctrDim: () => ({ width: 1280, height: 800 }),
                    getIsTxtEditor: () => true,
                },
                stubs: {
                    'sql-editor': "<div class='stub'></div>",
                    'readonly-sql-editor': "<div class='stub'></div>",
                },
            },
            opts
        )
    )
describe('QueryEditor', () => {
    let wrapper

    describe('QueryEditor created hook tests', () => {
        let handleAutoClearQueryHistoryCallCount = 0,
            validatingConnCallCount = 0
        before(() => {
            mountFactory({
                shallow: true,
                methods: {
                    ...stubModuleMethods,
                    handleAutoClearQueryHistory: () => handleAutoClearQueryHistoryCallCount++,
                    validatingConn: () => validatingConnCallCount++,
                },
            })
        })

        it(`Should call 'handleAutoClearQueryHistory' action once when
        component is created`, () => {
            expect(handleAutoClearQueryHistoryCallCount).to.be.equals(1)
        })
        it('Should call `validatingConn` action once when component is created', () => {
            expect(validatingConnCallCount).to.be.equals(1)
        })
    })

    describe('Leaving page tests', () => {
        it(`Should open confirmation dialog on leaving page when there
          is an active connection`, () => {
            wrapper = mountFactory({
                shallow: true,
                computed: {
                    // stub active connection
                    sql_conns: () => sql_conns_mock,
                },
                methods: stubModuleMethods,
            })
            mockBeforeRouteLeave(wrapper)
            expect(wrapper.vm.$data.isConfDlgOpened).to.be.true
        })
        it(`Should allow user to leave page when there is no active connection`, () => {
            wrapper = mountFactory({
                shallow: true,
                computed: {
                    // stub for having no active connection
                    sql_conns: () => ({}),
                },
                methods: stubModuleMethods,
            })
            expect(wrapper.vm.sql_conns).to.be.empty
            mockBeforeRouteLeave(wrapper)
            expect(wrapper.vm.$data.isConfDlgOpened).to.be.false
        })
        it(`Should emit leave-page when there is no active connection`, () => {
            wrapper = mountFactory({
                shallow: true,
                computed: {
                    // stub for having no active connection
                    sql_conns: () => ({}),
                },
                methods: stubModuleMethods,
            })
            mockBeforeRouteLeave(wrapper)
            expect(wrapper.emitted()).to.have.property('leave-page')
        })
    })

    describe('Handle disconnect connections when leaving page', () => {
        let disconnectAllSpy

        beforeEach(() => {
            disconnectAllSpy = sinon.spy(QueryEditor.methods, 'disconnectAll')
            wrapper = mountFactory({
                shallow: true,
                computed: {
                    // stub active connection
                    sql_conns: () => sql_conns_mock,
                },
                methods: stubModuleMethods,
            })
        })
        afterEach(() => {
            disconnectAllSpy.restore()
        })

        it(`Should disconnect all opened connections by default when
         confirming leaving the page`, () => {
            mockBeforeRouteLeave(wrapper) // mockup leaving page
            expect(wrapper.vm.$data.confirmDelAll).to.be.true
            // mock confirm leaving
            wrapper.vm.onLeave()
            disconnectAllSpy.should.have.been.calledOnce
        })

        it(`Should keep connections even when leaving the page`, async () => {
            mockBeforeRouteLeave(wrapper) //mockup leaving page
            // mockup un-checking "Disconnect all" checkbox
            await wrapper.setData({ confirmDelAll: false })
            // mock confirm leaving
            wrapper.vm.onLeave()
            disconnectAllSpy.should.have.not.been.called
        })
    })

    it('Should pass accurate data to wke-ctr component via props', () => {
        wrapper = mountFactory()
        const wke = wrapper.findAllComponents({ name: 'wke-ctr' }).at(0)
        expect(wke.vm.$props.ctrDim).to.be.equals(wrapper.vm.ctrDim)
    })

    describe('Should assign corresponding handler for worksheet shortcut keys accurately', () => {
        const evts = [
            'onCtrlEnter',
            'onCtrlShiftEnter',
            'onCtrlD',
            'onCtrlO',
            'onCtrlS',
            'onCtrlShiftS',
        ]
        evts.map(evt => {
            it(`Handle ${evt} evt`, () => {
                let callCount = 0
                wrapper = mountFactory({ methods: { [evt]: () => callCount++ } })
                const wke = wrapper.findAllComponents({ name: 'wke-ctr' }).at(0)
                wke.vm.$emit(evt)
                expect(callCount).to.be.equals(1)
            })
        })
    })
})
