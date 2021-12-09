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
    QueryPage.beforeRouteLeave.call(wrapper.vm, to_route_mock, from_route_mock, next)
}
describe('QueryPage index', () => {
    let wrapper, handleAutoClearQueryHistorySpy, validatingConnSpy, disconnectAllSpy

    beforeEach(() => {
        // spy on actions before mounting occurs
        handleAutoClearQueryHistorySpy = sinon.spy(QueryPage.methods, 'handleAutoClearQueryHistory')
        validatingConnSpy = sinon.spy(QueryPage.methods, 'validatingConn')
        disconnectAllSpy = sinon.spy(QueryPage.methods, 'disconnectAll')
        wrapper = mount({
            shallow: true,
            component: QueryPage,
        })
    })
    afterEach(() => {
        handleAutoClearQueryHistorySpy.restore()
        validatingConnSpy.restore()
        disconnectAllSpy.restore()
    })

    it('Should call `handleAutoClearQueryHistory` action once when component is created', () => {
        handleAutoClearQueryHistorySpy.should.have.been.calledOnce
    })
    it('Should call `validatingConn` action once when component is created', () => {
        validatingConnSpy.should.have.been.calledOnce
    })
    it(`Should open confirmation dialog on leaving page when there
      is an active connection`, () => {
        // mockup to have active connection
        wrapper.vm.$store.commit('query/SET_CNCT_RESOURCES', cnct_resources_mock)
        mockBeforeRouteLeave(wrapper)
        expect(wrapper.vm.$data.isConfDlgOpened).to.be.true
    })
    it(`Should allow user to leave page when there is no active connection`, () => {
        // mockup to have no active connection
        wrapper.vm.$store.commit('query/SET_CNCT_RESOURCES', [])
        expect(wrapper.vm.cnct_resources).to.be.empty
        mockBeforeRouteLeave(wrapper)
        expect(wrapper.vm.$data.isConfDlgOpened).to.be.false
    })

    it(`Should disconnect all opened connections by default when
     confirming leaving the page`, () => {
        // mockup leaving page
        wrapper.vm.$store.commit('query/SET_CNCT_RESOURCES', cnct_resources_mock)
        mockBeforeRouteLeave(wrapper)
        expect(wrapper.vm.$data.confirmDelAll).to.be.true
        // mock confirm leaving
        wrapper.vm.onLeave()
        disconnectAllSpy.should.have.been.calledOnce
    })

    it(`Should keep connections even when leaving the page`, () => {
        // mockup leaving page
        wrapper.vm.$store.commit('query/SET_CNCT_RESOURCES', cnct_resources_mock)
        mockBeforeRouteLeave(wrapper)
        // mockup un-checking "Disconnect all" checkbox
        wrapper.setData({
            confirmDelAll: false,
        })
        // mock confirm leaving
        wrapper.vm.onLeave()
        disconnectAllSpy.should.have.not.been.called
    })
})
