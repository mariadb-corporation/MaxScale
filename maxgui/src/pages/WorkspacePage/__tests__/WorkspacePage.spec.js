/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import WorkspacePage from '../index.vue'
import { lodash } from '@share/utils/helpers'
import QueryConn from '@wsModels/QueryConn'

const allConnsMock = [
    {
        id: '1',
        name: 'Read-Write-Listener',
        type: 'listeners',
    },
]
const from_route_mock = { name: 'workspace', path: '/workspace' }
const to_route_mock = { name: 'settings', path: '/settings' }
function mockBeforeRouteLeave(wrapper) {
    const next = sinon.stub()
    const beforeRouteLeave = wrapper.vm.$options.beforeRouteLeave[0]
    beforeRouteLeave.call(wrapper.vm, to_route_mock, from_route_mock, next)
}
const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: false,
                component: WorkspacePage,
                stubs: {
                    'mxs-workspace': "<div class='stub'></div>",
                },
            },
            opts
        )
    )
describe('WorkspacePage', () => {
    let wrapper

    describe('Created hook tests', () => {
        it('Should call `validateConns` action once when component is created', () => {
            const spy = sinon.spy(QueryConn, 'dispatch')
            wrapper = mountFactory({ shallow: true })
            spy.should.have.been.calledOnceWith('validateConns')
            spy.restore()
        })
    })
    describe('Leaving page tests', () => {
        it(`Should open confirmation dialog on leaving page when there
          is an active connection`, () => {
            wrapper = mountFactory({
                shallow: true,
                computed: {
                    // stub active connection
                    allConns: () => allConnsMock,
                },
            })
            mockBeforeRouteLeave(wrapper)
            expect(wrapper.vm.$data.isConfDlgOpened).to.be.true
        })
        it(`Should allow user to leave page when there is no active connection`, () => {
            wrapper = mountFactory({
                shallow: true,
                computed: {
                    // stub for having no active connection
                    allConns: () => [],
                },
            })
            mockBeforeRouteLeave(wrapper)
            expect(wrapper.vm.$data.isConfDlgOpened).to.be.false
        })
        it(`Should call leavePage when there is no active connection`, () => {
            wrapper = mountFactory({
                shallow: true,
                computed: {
                    // stub for having no active connection
                    allConns: () => [],
                },
                methods: { leavePage: () => sinon.stub() },
            })
            const spy = sinon.spy(wrapper.vm, 'leavePage')
            mockBeforeRouteLeave(wrapper)
            spy.should.have.been.calledOnce
        })
    })

    describe('Handle disconnect connections when leaving page', () => {
        let spy
        beforeEach(() => {
            wrapper = mountFactory({
                shallow: true,
                computed: {
                    // stub active connection
                    allConns: () => allConnsMock,
                },
            })
            spy = sinon.spy(QueryConn, 'dispatch')
        })
        afterEach(() => spy.restore())

        it(`Should disconnect all opened connections by default when
         confirming leaving the page`, () => {
            mockBeforeRouteLeave(wrapper)
            let shouldDelAll = true
            // mock confirm leaving
            wrapper.vm.onConfirm(shouldDelAll)
            spy.should.have.been.calledWith('disconnectAll')
        })

        it(`Should keep connections even when leaving the page`, async () => {
            mockBeforeRouteLeave(wrapper)
            // mock confirm leaving
            wrapper.vm.onConfirm()
            spy.should.have.not.been.called
        })
    })
})
