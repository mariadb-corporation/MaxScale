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
import QueryPage from '@/pages/QueryPage'

const cnct_resources_mock = [
    {
        id: '1',
        name: 'Read-Write-Listener',
        type: 'listeners',
    },
]
const from_route_mock = { name: 'queryEditor', path: '/query' }
const to_route_mock = { name: 'settings', path: '/settings' }
function mockBeforeRouteLeave(wrapper) {
    const next = sinon.stub()
    wrapper.vm.$options.beforeRouteLeave[0].call(wrapper.vm, to_route_mock, from_route_mock, next)
}
describe('QueryPage mounting tests', () => {
    let handleAutoClearQueryHistorySpy, validatingConnSpy
    before(() => {
        // spy on actions before mounting occurs
        handleAutoClearQueryHistorySpy = sinon.spy(QueryPage.methods, 'handleAutoClearQueryHistory')
        validatingConnSpy = sinon.spy(QueryPage.methods, 'validatingConn')
        mount({ shallow: true, component: QueryPage })
    })
    after(() => {
        handleAutoClearQueryHistorySpy.restore()
        validatingConnSpy.restore()
    })

    it('Should call `handleAutoClearQueryHistory` action once when component is created', () => {
        handleAutoClearQueryHistorySpy.should.have.been.calledOnce
    })
    it('Should call `validatingConn` action once when component is created', () => {
        validatingConnSpy.should.have.been.calledOnce
    })
})

describe('QueryPage leaving tests', () => {
    let wrapper

    it(`Should open confirmation dialog on leaving page when there
      is an active connection`, () => {
        wrapper = mount({
            shallow: true,
            component: QueryPage,
            computed: {
                // stub active connection
                cnct_resources: () => cnct_resources_mock,
            },
        })
        mockBeforeRouteLeave(wrapper)
        expect(wrapper.vm.$data.isConfDlgOpened).to.be.true
    })
    it(`Should allow user to leave page when there is no active connection`, () => {
        wrapper = mount({
            shallow: true,
            component: QueryPage,
            computed: {
                // stub for having no active connection
                cnct_resources: () => [],
            },
        })
        expect(wrapper.vm.cnct_resources).to.be.empty
        mockBeforeRouteLeave(wrapper)
        expect(wrapper.vm.$data.isConfDlgOpened).to.be.false
    })
})

describe('QueryPage - handle disconnect connections when leaving page', () => {
    let wrapper, disconnectAllSpy

    beforeEach(() => {
        disconnectAllSpy = sinon.spy(QueryPage.methods, 'disconnectAll')
    })
    afterEach(() => {
        disconnectAllSpy.restore()
    })

    it(`Should disconnect all opened connections by default when
     confirming leaving the page`, () => {
        wrapper = mount({
            shallow: true,
            component: QueryPage,
            computed: {
                // stub active connection
                cnct_resources: () => cnct_resources_mock,
            },
        })
        mockBeforeRouteLeave(wrapper) // mockup leaving page
        expect(wrapper.vm.$data.confirmDelAll).to.be.true
        // mock confirm leaving
        wrapper.vm.onLeave()
        disconnectAllSpy.should.have.been.calledOnce
    })

    it(`Should keep connections even when leaving the page`, async () => {
        wrapper = mount({
            shallow: true,
            component: QueryPage,
            computed: {
                // stub active connection
                cnct_resources: () => cnct_resources_mock,
            },
        })
        mockBeforeRouteLeave(wrapper) //mockup leaving page
        // mockup un-checking "Disconnect all" checkbox
        await wrapper.setData({ confirmDelAll: false })
        // mock confirm leaving
        wrapper.vm.onLeave()
        disconnectAllSpy.should.have.not.been.called
    })
})
