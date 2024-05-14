/* eslint-disable no-unused-vars */
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import ConnManCtr from '../ConnManCtr.vue'
import { itemSelectMock } from '@tests/unit/utils'

const dummy_sql_conns = {
    1: {
        id: '1',
        name: 'server_0',
        type: 'servers',
        binding_type: 'WORKSHEET',
        wke_id_fk: 'WKE_ID_123',
    },
    2: {
        id: '2',
        name: 'server_1',
        type: 'servers',
        binding_type: 'WORKSHEET',
        wke_id_fk: 'WKE_ID_456',
    },
    3: {
        id: '3',
        name: 'server_2',
        type: 'servers',
        binding_type: 'WORKSHEET',
        wke_id_fk: '',
    },
}

const mountFactory = opts =>
    mount({
        shallow: true,
        component: ConnManCtr,
        ...opts,
    })

// To have an active connection, getCurrWkeConn should have value
function mockActiveConnState() {
    return {
        getWkeConns: () => Object.values(dummy_sql_conns),
        getCurrWkeConn: () => dummy_sql_conns['1'],
        getActiveSessionId: () => 'SESSION_123_45',
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
describe(`ConnManCtr - child component's data communication tests `, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory()
    })
    it(`Should render reconn-dlg-ctr`, () => {
        expect(wrapper.findComponent({ name: 'reconn-dlg-ctr' }).exists()).to.be.true
    })
    it(`Should pass accurate data to conn-dlg-ctr via props`, () => {
        const { value, connOptions, handleSave } = wrapper.findComponent({
            name: 'conn-dlg-ctr',
        }).vm.$props
        expect(value).to.be.equals(wrapper.vm.isConnDlgOpened)
        expect(connOptions).to.be.deep.equals(wrapper.vm.connOptions)
        expect(handleSave).to.be.equals(wrapper.vm.handleOpenConn)
    })
    it(`Should pass accurate data to mxs-conf-dlg via props`, () => {
        const confirmDlg = wrapper.findComponent({ name: 'mxs-conf-dlg' }).vm
        const { type, item } = confirmDlg.$props
        const { value, title, closeImmediate, onSave } = confirmDlg.$attrs
        expect(value).to.be.equals(wrapper.vm.isConfDlgOpened)
        expect(title).to.be.equals(wrapper.vm.$mxs_t('disconnectConn'))
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

        expect(value).to.be.equals(wrapper.vm.$data.chosenWkeConn)
        expect(items).to.be.deep.equals(wrapper.vm.connOptions)
        expect(itemText).to.be.equals('name')
        expect(returnObject).to.be.true
        expect(placeholder).to.be.equals(wrapper.vm.$mxs_t('selectConnection'))
        expect(noDataText).to.be.equals(wrapper.vm.$mxs_t('noConnAvail'))
        expect(disabled).to.be.equals(wrapper.vm.getIsConnBusy)
    })
})

describe(`ConnManCtr - on created hook tests `, () => {
    let wrapper
    it(`Should auto open conn-dlg-ctr if no connection has been found
    and no pre_select_conn_rsrc after validating connections`, () => {
        wrapper = mountFactory({
            computed: {
                pre_select_conn_rsrc: () => null,
            },
        })
        expect(wrapper.vm.isConnDlgOpened).to.be.true
    })
    it(`Should call handlePreSelectConnRsrc if pre_select_conn_rsrc has value`, () => {
        const fnSpy = sinon.spy(ConnManCtr.methods, 'handlePreSelectConnRsrc')
        const preSelectConnRsrcStub = { id: 'server_1', type: 'servers' }
        wrapper = mountFactory({ computed: { pre_select_conn_rsrc: () => preSelectConnRsrcStub } })
        fnSpy.should.have.been.calledOnce
        fnSpy.restore()
    })

    it(`Should call assignActiveWkeConn if there is an active connection bound
    to the worksheet`, () => {
        const fnSpy = sinon.spy(ConnManCtr.methods, 'assignActiveWkeConn')
        wrapper = mountFactory({ computed: { ...mockActiveConnState() } })
        fnSpy.should.have.been.calledOnce
        fnSpy.restore()
    })
})

describe(`ConnManCtr - methods and computed properties tests `, () => {
    let wrapper
    afterEach(() => {
        wrapper.destroy()
    })
    it(`Should call onSelectConn if there is available connection has name
    equals to pre_select_conn_rsrc `, () => {
        const fnSpy = sinon.spy(ConnManCtr.methods, 'onSelectConn')
        const preSelectConnRsrcStub = { id: 'server_1', type: 'servers' }
        wrapper = mountFactory({
            computed: {
                //stub availableConnOpts so that  server_1 is available to use
                availableConnOpts: () => Object.values(dummy_sql_conns),
                pre_select_conn_rsrc: () => preSelectConnRsrcStub,
            },
            methods: {
                SET_ACTIVE_SQL_CONN: () => null,
                assignActiveWkeConn: () => null,
            },
        })
        fnSpy.should.have.been.calledOnceWith(
            wrapper.vm.availableConnOpts.find(cnn => cnn.name === preSelectConnRsrcStub.id)
        )
        fnSpy.restore()
    })
    it(`Should call openConnDialog if there is no available connection has name
    equals to pre_select_conn_rsrc `, () => {
        const fnSpy = sinon.spy(ConnManCtr.methods, 'openConnDialog')
        const preSelectConnRsrcStub = { id: 'server_1', type: 'servers' }
        wrapper = mountFactory({ computed: { pre_select_conn_rsrc: () => preSelectConnRsrcStub } })
        fnSpy.should.have.been.calledOnce
        fnSpy.restore()
    })
    it(`Should assign getCurrWkeConn value to chosenWkeConn if there is an active connection
      bound to the worksheet`, () => {
        wrapper = mountFactory({ computed: { ...mockActiveConnState() } })
        expect(wrapper.vm.chosenWkeConn).to.be.deep.equals(wrapper.vm.getCurrWkeConn)
    })
    it(`Should assign an empty object to chosenWkeConn if there is no active connection
      bound to the worksheet`, () => {
        wrapper = mountFactory({ computed: { getCurrWkeConn: () => ({}) } })
        wrapper.vm.$nextTick(() => {
            expect(wrapper.vm.chosenWkeConn).to.be.an('object').and.be.empty
        })
    })
    it(`Should disabled connections that are bound to a worksheet in connOptions`, () => {
        wrapper = mountFactory({ computed: { ...mockActiveConnState() } })
        expect(wrapper.vm.connOptions[1]).to.have.property('disabled')
        expect(wrapper.vm.connOptions[1].disabled).to.be.true
    })
    it(`Should return accurate value for connToBeDel computed property`, () => {
        wrapper = mountFactory({ computed: { ...mockActiveConnState() } })
        // mock unlinkConn call
        wrapper.vm.unlinkConn(wrapper.vm.connOptions[0])
        expect(wrapper.vm.connToBeDel).to.be.deep.equals({ id: dummy_sql_conns['1'].name })
    })
})

describe(`ConnManCtr - connection list dropdown tests`, () => {
    let wrapper
    it(`Should call onSelectConn method when select new connection`, async () => {
        const onSelectConnSpy = sinon.spy(ConnManCtr.methods, 'onSelectConn')
        wrapper = mountFactory({
            shallow: false,
            computed: { ...mockActiveConnState() },
            methods: { SET_ACTIVE_SQL_CONN: () => null },
        })
        await itemSelectMock(wrapper, wrapper.vm.connOptions[1], '.conn-dropdown')
        onSelectConnSpy.should.have.been.calledOnce
        onSelectConnSpy.restore()
    })
})

describe(`ConnManCtr - other tests`, () => {
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
        const disconnectSpy = sinon.spy(ConnManCtr.methods, 'disconnect')
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
            let openConnectArgs
            wrapper = mountFactory({
                methods: { openConnect: args => (openConnectArgs = args) },
            })
            wrapper.vm.handleOpenConn(mockNewConnData())
            expect(openConnectArgs).to.be.deep.equals(mockNewConnData())
        })
    })
})
