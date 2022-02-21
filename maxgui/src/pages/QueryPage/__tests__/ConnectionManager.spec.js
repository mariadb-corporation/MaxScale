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
import { itemSelectMock } from '@tests/unit/utils'

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

function mockNewConnData() {
    return {
        body: {
            target: 'server_0',
            user: 'maxskysql',
            password: 'skysql',
            db: '',
            timeout: 300,
        },
        resourceType: 'servers',
    }
}
describe(`ConnectionManager - child component's data communication tests `, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory()
    })
    it(`Should render reconn-dialog`, () => {
        expect(wrapper.findComponent({ name: 'reconn-dialog' }).exists()).to.be.true
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
        const confirmDlg = wrapper.findComponent({ name: 'confirm-dialog' }).vm
        const { type, item } = confirmDlg.$props
        const { value, title, closeImmediate, onSave } = confirmDlg.$attrs
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
    it(`Should auto open connection-dialog if no connection has been found
    and no pre_select_conn_rsrc after validating connections`, () => {
        wrapper = mountFactory({
            computed: {
                pre_select_conn_rsrc: () => null,
            },
        })
        expect(wrapper.vm.isConnDialogOpened).to.be.true
    })
    it(`Should call handlePreSelectConnRsrc if pre_select_conn_rsrc has value`, () => {
        const fnSpy = sinon.spy(ConnectionManager.methods, 'handlePreSelectConnRsrc')
        const preSelectConnRsrcStub = { id: 'server_1', type: 'servers' }
        wrapper = mountFactory({ computed: { pre_select_conn_rsrc: () => preSelectConnRsrcStub } })
        fnSpy.should.have.been.calledOnce
        fnSpy.restore()
    })
    const fnCalls = ['initialFetch', 'assignActiveConn']
    fnCalls.forEach(fn => {
        it(`Should call ${fn} if there is an active connection bound to the worksheet`, () => {
            const fnSpy = sinon.spy(ConnectionManager.methods, fn)
            wrapper = mountFactory({ computed: { ...mockActiveConnState() } })
            fnSpy.should.have.been.calledOnce
            fnSpy.restore()
        })
    })
})

describe(`ConnectionManager - methods and computed properties tests `, () => {
    let wrapper
    it(`Should call onChangeChosenConn if there is available connection has name
    equals to pre_select_conn_rsrc `, () => {
        const fnSpy = sinon.spy(ConnectionManager.methods, 'onChangeChosenConn')
        const preSelectConnRsrcStub = { id: 'server_1', type: 'servers' }
        wrapper = mountFactory({
            computed: {
                //stub availableConnOpts so that  server_1 is available to use
                availableConnOpts: () => dummy_cnct_resources,
                pre_select_conn_rsrc: () => preSelectConnRsrcStub,
            },
            methods: {
                SET_CURR_CNCT_RESOURCE: () => null,
                updateRoute: () => null,
                handleDispatchInitialFetch: () => null,
            },
        })
        fnSpy.should.have.been.calledOnceWith(
            dummy_cnct_resources.find(cnn => cnn.name === preSelectConnRsrcStub.id)
        )
        fnSpy.restore()
    })
    it(`Should call openConnDialog if there is no available connection has name
    equals to pre_select_conn_rsrc `, () => {
        const fnSpy = sinon.spy(ConnectionManager.methods, 'openConnDialog')
        const preSelectConnRsrcStub = { id: 'server_1', type: 'servers' }
        wrapper = mountFactory({ computed: { pre_select_conn_rsrc: () => preSelectConnRsrcStub } })
        fnSpy.should.have.been.calledOnce
        fnSpy.restore()
    })
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

describe(`ConnectionManager - connection list dropdown tests`, () => {
    let wrapper
    it(`Should call onChangeChosenConn method when select new connection`, async () => {
        const onSelectConnSpy = sinon.spy(ConnectionManager.methods, 'onChangeChosenConn')
        wrapper = mountFactory({
            shallow: false,
            computed: { ...mockActiveConnState() },
            methods: { SET_CURR_CNCT_RESOURCE: () => null, initialFetch: () => null },
        })
        await itemSelectMock(wrapper, wrapper.vm.connOptions[1], '.conn-dropdown')
        onSelectConnSpy.should.have.been.calledOnce
        onSelectConnSpy.restore()
    })
    it(`Should call SET_CURR_CNCT_RESOURCE and updateRoute with accurate arguments
      when onChangeChosenConn is called`, () => {
        let updateRouteArgs, setCurrCnctResourceArgs, handleDispatchInitialFetchArgs
        wrapper = mountFactory({
            computed: { ...mockActiveConnState() },
            methods: {
                SET_CURR_CNCT_RESOURCE: v => (setCurrCnctResourceArgs = v),
                updateRoute: v => (updateRouteArgs = v),
                handleDispatchInitialFetch: v => (handleDispatchInitialFetchArgs = v),
            },
        })
        const selectConn = wrapper.vm.connOptions[1]
        wrapper.vm.onChangeChosenConn(selectConn)
        expect(updateRouteArgs).to.be.deep.equals(wrapper.vm.active_wke_id)
        expect(setCurrCnctResourceArgs).to.be.deep.equals({
            payload: selectConn,
            active_wke_id: wrapper.vm.active_wke_id,
        })
        expect(handleDispatchInitialFetchArgs).to.be.deep.equals(selectConn)
    })
})

describe(`ConnectionManager - other tests`, () => {
    let wrapper
    it(`Should open confirm dialog when unlinkConn method is called`, () => {
        wrapper = mountFactory({
            computed: { ...mockActiveConnState() },
        })
        const connToBeDeleted = wrapper.vm.connOptions[0]
        wrapper.vm.unlinkConn(connToBeDeleted)
        expect(wrapper.vm.isConfDlgOpened).to.be.true
        expect(wrapper.vm.targetConn).to.be.deep.equals(connToBeDeleted)
    })
    it(`Should call disconnect action with accurate args when
      confirm deleting a connection`, () => {
        const connToBeDeleted = wrapper.vm.connOptions[0]
        const disconnectSpy = sinon.spy(ConnectionManager.methods, 'disconnect')
        wrapper = mountFactory({
            shallow: false,
            computed: { ...mockActiveConnState() },
            // mock opening confirm dialog
            data: () => ({ isConfDlgOpened: true, targetConn: connToBeDeleted }),
        })
        wrapper.vm.confirmDelConn()
        disconnectSpy.should.have.been.calledOnceWith({
            showSnackbar: true,
            id: connToBeDeleted.id,
        })
        disconnectSpy.restore()
    })

    const handleOpenConnCases = [
        'worksheet is bound to a connection',
        'worksheet is not bound to a connection',
    ]
    handleOpenConnCases.forEach(c => {
        it(`Should handle dispatch openConnect action accurately when ${c}`, () => {
            const initialFetchSpy = sinon.spy(ConnectionManager.methods, 'initialFetch')
            let openConnectArgs
            const hasConnectionAlready = c === 'worksheet is bound to a connection'
            wrapper = mountFactory({
                computed: { ...(hasConnectionAlready ? mockActiveConnState() : {}) },
                methods: { openConnect: args => (openConnectArgs = args) },
            })
            wrapper.vm.handleOpenConn(mockNewConnData())
            expect(openConnectArgs).to.be.deep.equals(mockNewConnData())
            hasConnectionAlready
                ? initialFetchSpy.should.have.been.calledOnce
                : initialFetchSpy.should.have.not.been.called
            initialFetchSpy.restore()
        })
    })
})
