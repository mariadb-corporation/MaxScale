/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import WkeConnMan from '../WkeConnMan.vue'
import { itemSelectMock } from '@tests/unit/utils'

const dummyWkeConns = [
    {
        id: '1',
        active_db: 'test',
        attributes: {},
        binding_type: 'WORKSHEET',
        name: 'server_0',
        type: 'servers',
        meta: {},
        clone_of_conn_id: null,
        worksheet_id: 'WKE_ID_123',
        query_tab_id: null,
    },
    {
        id: '2',
        active_db: '',
        attributes: {},
        binding_type: 'WORKSHEET',
        name: 'server_1',
        type: 'servers',
        meta: {},
        clone_of_conn_id: null,
        worksheet_id: 'WKE_ID_456',
        query_tab_id: null,
    },
    {
        id: '3',
        active_db: '',
        attributes: {},
        binding_type: 'WORKSHEET',
        name: 'server_2',
        type: 'servers',
        meta: {},
        clone_of_conn_id: null,
        worksheet_id: null,
        query_tab_id: 'QUERY_TAB_ID',
    },
]

const mountFactory = opts =>
    mount({
        shallow: true,
        component: WkeConnMan,
        ...opts,
    })

// To have an active connection, activeWkeConn should have value
function mockActiveConnState() {
    return {
        wkeConns: () => dummyWkeConns,
        activeWkeConn: () => dummyWkeConns[0],
    }
}

describe(`WkeConnMan - child component's data communication tests `, () => {
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
        expect(disabled).to.be.equals(wrapper.vm.isConnBusy)
    })
})

describe(`WkeConnMan - on created hook tests `, () => {
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
        const fnSpy = sinon.spy(WkeConnMan.methods, 'handlePreSelectConnRsrc')
        const preSelectConnRsrcStub = { id: 'server_1', type: 'servers' }
        wrapper = mountFactory({ computed: { pre_select_conn_rsrc: () => preSelectConnRsrcStub } })
        fnSpy.should.have.been.calledOnce
        fnSpy.restore()
    })

    it(`Should call assignActiveWkeConn if there is an active connection bound
    to the worksheet`, () => {
        const fnSpy = sinon.spy(WkeConnMan.methods, 'assignActiveWkeConn')
        wrapper = mountFactory({ computed: { ...mockActiveConnState() } })
        fnSpy.should.have.been.calledOnce
        fnSpy.restore()
    })
})

describe(`WkeConnMan - methods and computed properties tests `, () => {
    let wrapper
    afterEach(() => {
        wrapper.destroy()
    })
    it(`Should call onSelectConn if there is available connection has name
    equals to pre_select_conn_rsrc `, () => {
        const fnSpy = sinon.spy(WkeConnMan.methods, 'onSelectConn')
        const preSelectConnRsrcStub = { id: 'server_1', type: 'servers' }
        wrapper = mountFactory({
            computed: {
                //stub availableConnOpts so that  server_1 is available to use
                availableConnOpts: () => dummyWkeConns,
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
        const fnSpy = sinon.spy(WkeConnMan.methods, 'openConnDialog')
        const preSelectConnRsrcStub = { id: 'server_1', type: 'servers' }
        wrapper = mountFactory({ computed: { pre_select_conn_rsrc: () => preSelectConnRsrcStub } })
        fnSpy.should.have.been.calledOnce
        fnSpy.restore()
    })
    it(`Should assign activeWkeConn value to chosenWkeConn if there is an active connection
      bound to the worksheet`, () => {
        wrapper = mountFactory({ computed: { ...mockActiveConnState() } })
        expect(wrapper.vm.chosenWkeConn).to.be.deep.equals(wrapper.vm.activeWkeConn)
    })
    it(`Should assign an empty object to chosenWkeConn if there is no active connection
      bound to the worksheet`, () => {
        wrapper = mountFactory({ computed: { activeWkeConn: () => ({}) } })
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
        expect(wrapper.vm.connToBeDel).to.be.deep.equals({ id: wrapper.vm.connOptions[0].name })
    })
})

describe(`WkeConnMan - connection list dropdown tests`, () => {
    let wrapper
    it(`Should call onSelectConn method when select new connection`, async () => {
        const onSelectConnSpy = sinon.spy(WkeConnMan.methods, 'onSelectConn')
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

describe(`WkeConnMan - other tests`, () => {
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
})
