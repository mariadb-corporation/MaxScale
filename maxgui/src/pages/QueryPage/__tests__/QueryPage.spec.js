/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import QueryPage from '../index.vue'
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
    const beforeRouteLeave = wrapper.vm.$options.beforeRouteLeave[0]
    beforeRouteLeave.call(wrapper.vm, to_route_mock, from_route_mock, next)
}
const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: false,
                component: QueryPage,
                stubs: {
                    'query-editor': "<div class='stub'></div>",
                },
            },
            opts
        )
    )
describe('QueryPage', () => {
    let wrapper

    describe('Created hook tests', () => {
        let validatingConnCallCount = 0
        before(() => {
            mountFactory({
                shallow: true,
                methods: {
                    validateConns: () => validatingConnCallCount++,
                },
            })
        })
        it('Should call `validateConns` action once when component is created', () => {
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
                methods: {
                    leavePage: () => sinon.stub(),
                },
            })
            const spy = sinon.spy(wrapper.vm, 'leavePage')
            mockBeforeRouteLeave(wrapper)
            spy.should.have.been.calledOnce
        })
    })

    describe('Handle disconnect connections when leaving page', () => {
        let disconnectAllSpy

        beforeEach(() => {
            disconnectAllSpy = sinon.spy(QueryPage.methods, 'disconnectAll')
            wrapper = mountFactory({
                shallow: true,
                computed: {
                    // stub active connection
                    sql_conns: () => sql_conns_mock,
                },
            })
        })
        afterEach(() => {
            disconnectAllSpy.restore()
        })

        it(`Should disconnect all opened connections by default when
         confirming leaving the page`, () => {
            mockBeforeRouteLeave(wrapper) // mockup leaving page
            expect(wrapper.vm.$data.shouldDelAll).to.be.true
            // mock confirm leaving
            wrapper.vm.onLeave()
            disconnectAllSpy.should.have.been.calledOnce
        })

        it(`Should keep connections even when leaving the page`, async () => {
            mockBeforeRouteLeave(wrapper) //mockup leaving page
            // mockup un-checking "Disconnect all" checkbox
            await wrapper.setData({ shouldDelAll: false })
            // mock confirm leaving
            wrapper.vm.onLeave()
            disconnectAllSpy.should.have.not.been.called
        })
    })
})
