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
import ConnectionManager from '@/pages/QueryPage/ConnectionManager'

const dummy_cnct_resources = [
    { id: '1', name: 'server_0', type: 'servers' },
    { id: '2', name: 'server_1', type: 'servers' },
]

const mountFactory = opts =>
    mount({
        shallow: true,
        component: ConnectionManager,
        ...opts,
    })

// To have an active connection, curr_cnct_resource and cnct_resources should have value
function mockActiveConnState() {
    return {
        cnct_resources: () => dummy_cnct_resources,
        curr_cnct_resource: () => dummy_cnct_resources[0],
    }
}
describe(`ConnectionManager - child component's data communication tests `, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory()
    })
    it(`Should pass accurate data to connection-dialog via props`, () => {
        const { value, connOptions, handleSave } = wrapper.findComponent({
            name: 'connection-dialog',
        }).vm.$props
        expect(value).to.be.equals(wrapper.vm.isConnDialogOpened)
        expect(connOptions).to.be.deep.equals(wrapper.vm.connOptions)
        expect(handleSave).to.be.equals(wrapper.vm.handleOpenConn)
    })
    it(`Should pass accurate data to confirm-dialog via props`, () => {
        const { value, title, type, closeImmediate, item, onSave } = wrapper.findComponent({
            name: 'confirm-dialog',
        }).vm.$props
        expect(value).to.be.equals(wrapper.vm.isConfDlgOpened)
        expect(title).to.be.equals(wrapper.vm.$t('disconnectConn'))
        expect(type).to.be.equals('disconnect')
        expect(closeImmediate).to.be.true
        expect(item).to.be.deep.equals(wrapper.vm.connToBeDel)
        expect(onSave).to.be.equals(wrapper.vm.confirmDelConn)
    })
    it(`Should pass accurate data to v-select (connection list dropdown) via props`, () => {
        const {
            value,
            items,
            itemText,
            returnObject,
            placeholder,
            noDataText,
            disabled,
        } = wrapper.findComponent({ name: 'v-select' }).vm.$props

        expect(value).to.be.equals(wrapper.vm.$data.chosenConn)
        expect(items).to.be.deep.equals(wrapper.vm.connOptions)
        expect(itemText).to.be.equals('name')
        expect(returnObject).to.be.true
        expect(placeholder).to.be.equals(wrapper.vm.$t('selectConnection'))
        expect(noDataText).to.be.equals(wrapper.vm.$t('noConnAvail'))
        expect(disabled).to.be.equals(wrapper.vm.$props.disabled)
    })
})

describe(`ConnectionManager - on created hook tests `, () => {
    let wrapper
    it(`Should auto open connection-dialog if no connection found after
    validating connections`, () => {
        wrapper = mountFactory()
        expect(wrapper.vm.isConnDialogOpened).to.be.true
    })
    const fnCalls = ['initialFetch', 'assignActiveConn']
    fnCalls.forEach(fn => {
        it(`Should call ${fn} if there is an active connection bound to the worksheet`, () => {
            const fnSpy = sinon.spy(ConnectionManager.methods, fn)
            wrapper = mountFactory({ computed: { ...mockActiveConnState() } })
            fnSpy.should.have.been.calledOnce
        })
    })
})

describe(`ConnectionManager - methods and computed properties tests `, () => {
    let wrapper
    it(`Should assign curr_cnct_resource value to chosenConn if there is an active connection
      bound to the worksheet`, () => {
        wrapper = mountFactory({ computed: { ...mockActiveConnState() } })
        expect(wrapper.vm.chosenConn).to.be.deep.equals(wrapper.vm.curr_cnct_resource)
    })
    it(`Should assign an empty object to chosenConn if there is no active connection
      bound to the worksheet`, () => {
        wrapper = mountFactory()
        expect(wrapper.vm.chosenConn).to.be.an('object').and.be.empty
    })
    it(`Should return accurate value for usedConnections computed property`, () => {
        wrapper = mountFactory({
            computed: { worksheets_arr: () => [{ curr_cnct_resource: dummy_cnct_resources[0] }] },
        })
        expect(wrapper.vm.usedConnections).to.be.deep.equals([dummy_cnct_resources[0].id])
    })
    it(`Should not disabled current connection that is bound to current worksheet
      in connOptions`, () => {
        wrapper = mountFactory({ computed: { ...mockActiveConnState() } })
        expect(wrapper.vm.connOptions[0]).to.be.deep.equals({
            // first obj in mockActiveConnState is used as curr_cnct_resource
            ...dummy_cnct_resources[0],
            disabled: false,
        })
    })
    it(`Should disabled connections that are bound to in active worksheet
      in connOptions`, () => {
        wrapper = mountFactory({
            computed: {
                ...mockActiveConnState(),
                usedConnections: () => [dummy_cnct_resources[1].id],
            },
        })
        expect(wrapper.vm.connOptions[1]).to.be.deep.equals({
            ...dummy_cnct_resources[1],
            disabled: true,
        })
    })

    it(`Should return accurate value for connToBeDel computed property`, () => {
        wrapper = mountFactory({
            computed: {
                ...mockActiveConnState(),
            },
        })
        // mock unlinkConn call
        wrapper.vm.unlinkConn(wrapper.vm.connOptions[0])
        expect(wrapper.vm.connToBeDel).to.be.deep.equals({ id: dummy_cnct_resources[0].name })
    })
})

//TODO: Add more tests
