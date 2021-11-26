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
import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import QueryPage from '@/pages/QueryPage'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import { routeChangesMock } from '@tests/unit/utils'

chai.should()
chai.use(sinonChai)
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
    let wrapper, handleAutoClearQueryHistorySpy, validatingConnSpy

    beforeEach(async () => {
        // spy on actions before mounting occurs
        handleAutoClearQueryHistorySpy = sinon.spy(QueryPage.methods, 'handleAutoClearQueryHistory')
        validatingConnSpy = sinon.spy(QueryPage.methods, 'validatingConn')
        wrapper = mount({
            shallow: true,
            component: QueryPage,
        })
        await routeChangesMock(wrapper, '/query') // Mockup leaving page
    })
    afterEach(async () => {
        handleAutoClearQueryHistorySpy.restore()
        validatingConnSpy.restore()
        await routeChangesMock(wrapper, '/settings') // Mockup leaving page
    })

    it('Should call `handleAutoClearQueryHistory` action once when component is created', () => {
        handleAutoClearQueryHistorySpy.should.have.been.calledOnce
    })
    it('Should call `validatingConn` action once when component is created', () => {
        validatingConnSpy.should.have.been.calledOnce
    })
    it(`Should open confirmation dialog on leaving page when there
      is an active connection`, async () => {
        // mockup to have active connection
        wrapper.vm.$store.commit('query/SET_CNCT_RESOURCES', cnct_resources_mock)
        mockBeforeRouteLeave(wrapper)
        expect(wrapper.vm.$data.isConfDlgOpened).to.be.true
    })
    it(`Should allow user to leave page when there is no active connection`, async () => {
        // mockup to have no active connection
        wrapper.vm.$store.commit('query/SET_CNCT_RESOURCES', [])
        expect(wrapper.vm.cnct_resources).to.be.empty
        mockBeforeRouteLeave(wrapper)
        expect(wrapper.vm.$data.isConfDlgOpened).to.be.false
    })
})
