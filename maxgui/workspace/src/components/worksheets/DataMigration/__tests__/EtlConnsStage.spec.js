/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import EtlConnsStage from '@wkeComps/DataMigration/EtlConnsStage'
import { lodash } from '@share/utils/helpers'
import { task } from '@wkeComps/DataMigration/__tests__/stubData'
import QueryConn from '@wsModels/QueryConn'
import EtlTask from '@wsModels/EtlTask'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: EtlConnsStage,
                propsData: { task, srcConn: {}, destConn: {}, hasConns: false },
            },
            opts
        )
    )

describe('EtlConnsStage', () => {
    let wrapper

    describe(`Child component's data communication tests`, () => {
        beforeEach(() => (wrapper = mountFactory()))
        it(`Should render mxs-stage-ctr`, () => {
            expect(wrapper.findComponent({ name: 'mxs-stage-ctr' }).exists()).to.be.true
        })

        it(`Should pass accurate data to odbc-form`, () => {
            const { value, drivers } = wrapper.findComponent({ name: 'odbc-form' }).vm.$props
            expect(value).to.eql(wrapper.vm.$data.src)
            expect(drivers).to.eql(wrapper.vm.odbc_drivers)
        })

        it(`Should pass accurate data to etl-dest-conn`, () => {
            const { value, allServers, destTargetType } = wrapper.findComponent({
                name: 'etl-dest-conn',
            }).vm.$props
            expect(value).to.eql(wrapper.vm.$data.dest)
            expect(allServers).to.eql(wrapper.vm.allServers)
            expect(destTargetType).to.eql(wrapper.vm.destTargetType)
        })

        it(`Should pass accurate data to etl-logs`, () => {
            const { task } = wrapper.findComponent({ name: 'etl-logs' }).vm.$props
            expect(task).to.eql(wrapper.vm.$props.task)
        })
    })
    describe(`Computed properties and created hook`, () => {
        afterEach(() => sinon.restore())

        it(`Should return accurate value for destTargetType`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.destTargetType).to.equal('servers')
        })

        it(`Should call expected methods on created hook`, async () => {
            const fetchOdbcDriversStub = sinon.stub(EtlConnsStage.methods, 'fetchOdbcDrivers')
            const fetchRcTargetNamesStub = sinon.stub(EtlConnsStage.methods, 'fetchRcTargetNames')
            wrapper = mountFactory()
            await wrapper.vm.$nextTick()
            fetchOdbcDriversStub.should.have.been.calledOnce
            fetchRcTargetNamesStub.should.have.been.calledOnceWith('servers')
        })
    })
    describe(`Method tests`, () => {
        afterEach(() => sinon.restore())

        it(`Should push log when handleOpenConns is called`, async () => {
            wrapper = mountFactory({
                propsData: {
                    hasConns: true,
                    srcConn: { id: '123' },
                    destConn: { id: '123' },
                },
            })
            const etlTaskStub = sinon.stub(EtlTask, 'dispatch')
            await wrapper.vm.handleOpenConns()
            etlTaskStub.should.have.been.calledOnceWith('pushLog')
        })

        const connectTestCases = [{ id: '123' }, {}]
        const connTypeTestCases = [
            { computedName: 'srcConn', handler: 'openSrcConn' },
            { computedName: 'destConn', handler: 'openDestConn' },
        ]
        connTypeTestCases.forEach(item => {
            connectTestCases.forEach(conn => {
                const hasConn = Boolean(conn.id)
                it(`Should ${hasConn ? 'not ' : ''}dispatch ${
                    item.handler
                } when handleOpenConns is called`, async () => {
                    wrapper = mountFactory({
                        propsData: { hasConns: false, [item.computedName]: conn },
                    })
                    const stub = sinon.stub(wrapper.vm, item.handler).resolves()
                    // Stub the other methods
                    sinon
                        .stub(
                            wrapper.vm,
                            item.handler === 'openSrcConn' ? 'openDestConn' : 'openSrcConn'
                        )
                        .resolves()
                    await wrapper.vm.handleOpenConns()
                    if (hasConn) stub.should.have.not.been.called
                    else stub.should.have.been.calledOnce
                })
            })
        })

        it(`Should show successfully message in the snackbar`, async () => {
            wrapper = mountFactory({
                propsData: { hasConns: true },
                methods: { openSrcConn: () => null, openDestConn: () => null },
            })
            const stub = sinon.stub(wrapper.vm, 'SET_SNACK_BAR_MESSAGE')
            await wrapper.vm.handleOpenConns()
            stub.should.have.been.calledOnceWith({
                text: [wrapper.vm.$mxs_t('success.connected')],
                type: 'success',
            })
        })

        it(`Should handle openSrcConn as expected`, async () => {
            wrapper = mountFactory()
            const stub = sinon.stub(QueryConn, 'dispatch')
            await wrapper.vm.openSrcConn()
            stub.calledOnceWithExactly('openEtlConn', {
                body: {
                    target: 'odbc',
                    connection_string: wrapper.vm.$data.src.connection_string,
                    timeout: wrapper.vm.$data.src.timeout,
                },
                binding_type: wrapper.vm.QUERY_CONN_BINDING_TYPES.ETL_SRC,
                etl_task_id: task.id,
                taskMeta: { src_type: wrapper.vm.$data.src.type },
                connMeta: { name: wrapper.vm.$data.src.type },
            })
        })

        it(`Should handle openDestConn as expected`, async () => {
            wrapper = mountFactory()
            const stub = sinon.stub(QueryConn, 'dispatch')
            await wrapper.vm.openDestConn()
            stub.calledOnceWithExactly('openEtlConn', {
                body: wrapper.vm.$data.dest,
                binding_type: wrapper.vm.QUERY_CONN_BINDING_TYPES.ETL_DEST,
                etl_task_id: task.id,
                taskMeta: { dest_name: wrapper.vm.dest.target },
                connMeta: { name: wrapper.vm.dest.target },
            })
        })

        it(`Should validate the form before opening the connections when
        the next method is called`, async () => {
            wrapper = mountFactory({ shallow: false, propsData: { hasConns: false } })
            const validateStub = sinon.stub(wrapper.vm.$refs.form, 'validate').resolves()
            const handleOpenConnsStub = sinon.stub(wrapper.vm, 'handleOpenConns').resolves()
            await wrapper.vm.next()
            validateStub.calledOnce
            handleOpenConnsStub.calledOnce
        })

        it(`Should validate the form before opening the connections when
        the next method is called`, async () => {
            wrapper = mountFactory({ shallow: false, propsData: { hasConns: true } })
            const etlTaskUpdateStub = sinon.stub(EtlTask, 'update')
            await wrapper.vm.next()
            etlTaskUpdateStub.calledOnce
        })
    })
})
