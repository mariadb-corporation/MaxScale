/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import EtlMigrationStage from '@wkeComps/DataMigration/EtlMigrationStage'
import { lodash } from '@share/utils/helpers'
import { task } from '@wkeComps/DataMigration/__tests__/stubData'
import EtlTask from '@wsModels/EtlTask'
import { ETL_STATUS } from '@wsSrc/constants'

const mountFactory = opts =>
    mount(
        lodash.merge(
            { shallow: true, component: EtlMigrationStage, propsData: { task, srcConn: {} } },
            opts
        )
    )

const etlResTableStub = [
    {
        create: 'create script',
        insert: 'insert script',
        schema: 'test',
        select: 'select script',
        table: 't1',
    },
]

describe('EtlMigrationStage', () => {
    let wrapper

    describe("Child component's data communication tests", () => {
        afterEach(() => sinon.restore())

        it(`Should render mxs-stage-ctr`, () => {
            wrapper = mountFactory()
            expect(wrapper.findComponent({ name: 'mxs-stage-ctr' }).exists()).to.be.true
        })

        it(`Should render stage header title`, () => {
            wrapper = mountFactory()
            expect(wrapper.find('[data-test="stage-header-title"]').text()).to.equal(
                wrapper.vm.$mxs_t('migration')
            )
        })

        it(`Should pass accurate data to etl-migration-manage`, () => {
            wrapper = mountFactory()
            expect(wrapper.findComponent({ name: 'etl-migration-manage' }).vm.$props.task).to.equal(
                wrapper.vm.$props.task
            )
        })

        it(`Should call onRestart if etl-migration-manage emit on-restart`, () => {
            wrapper = mountFactory()
            const stub = sinon.stub(wrapper.vm, 'onRestart')
            wrapper.findComponent({ name: 'etl-migration-manage' }).vm.$emit('on-restart', task.id)
            stub.calledOnceWithExactly(task.id)
        })

        it(`Should pass accurate data to etl-status-icon`, () => {
            wrapper = mountFactory({ computed: { srcSchemaTree: () => [] } })
            const { icon, spinning } = wrapper.find('[data-test="header-status-icon"]').vm.$props
            expect(icon).to.equal(wrapper.vm.$props.task.status)
            expect(spinning).to.equal(wrapper.vm.isRunning)
        })

        it(`Should conditionally show prepare script info`, () => {
            wrapper = mountFactory()
            const selector = '[data-test="prepare-script-info"]'
            expect(wrapper.find(selector).exists()).to.be.false
            wrapper = mountFactory({
                computed: { isPrepareEtl: () => true, isRunning: () => false },
            })
            expect(wrapper.find(selector).text()).to.equal(wrapper.vm.prepareScriptInfo)
        })

        it(`Should conditionally show general error`, () => {
            wrapper = mountFactory()
            const selector = '[data-test="general-err"]'
            expect(wrapper.find(selector).exists()).to.be.false
            wrapper = mountFactory({ computed: { generalErr: () => 'Errors' } })
            expect(wrapper.find(selector).text()).to.equal(wrapper.vm.generalErr)
        })

        it(`Should conditionally show creation stage error`, () => {
            wrapper = mountFactory()
            const selector = '[data-test="creation-stage-err"]'
            expect(wrapper.find(selector).exists()).to.be.false
            wrapper = mountFactory({ computed: { hasErrAtCreationStage: () => true } })
            expect(wrapper.find(selector).text()).to.equal(
                wrapper.vm.$mxs_t(`errors.etl_create_stage`)
            )
        })

        it(`Should conditionally show etl status as a fallback message`, () => {
            wrapper = mountFactory({ computed: { isPrepareEtl: () => true } })
            const selector = '[data-test="fallback-msg"]'
            expect(wrapper.find(selector).exists()).to.be.false
            wrapper = mountFactory({
                computed: {
                    generalErr: () => '',
                    hasErrAtCreationStage: () => false,
                    isPrepareEtl: () => false,
                    isRunning: () => true,
                },
            })
            const expectedText = `${wrapper.vm.$mxs_t(task.status.toLowerCase())} ...`
            expect(wrapper.find(selector).text()).to.equal(expectedText)
        })

        it(`Should conditionally render v-progress-linear`, () => {
            wrapper = mountFactory()
            const selector = { name: 'v-progress-linear' }
            expect(wrapper.findComponent(selector).exists()).to.be.false
            wrapper = mountFactory({
                computed: { isPrepareEtl: () => true, isRunning: () => true },
            })
            expect(wrapper.findComponent(selector).exists()).to.be.true
        })

        it(`Should conditionally render etl-logs`, () => {
            wrapper = mountFactory()
            const selector = { name: 'etl-logs' }
            let component = wrapper.findComponent(selector)
            expect(component.exists()).to.be.false
            wrapper = mountFactory({
                computed: { etlResTable: () => [], isInErrState: () => true },
            })
            component = wrapper.findComponent(selector)
            expect(component.exists()).to.be.true
            expect(component.vm.$props.task).to.eql(wrapper.vm.$props.task)
        })

        it(`Should pass accurate data to etl-tbl-script`, () => {
            wrapper = mountFactory({ computed: { etlResTable: () => etlResTableStub } })
            const {
                $props: { task, data },
                $attrs: { headers, ['custom-sort']: customSort },
            } = wrapper.findComponent({ name: 'etl-tbl-script' }).vm
            expect(task).to.eql(wrapper.vm.$props.task)
            expect(data).to.eql(wrapper.vm.etlResTable)
            expect(headers).to.eql(wrapper.vm.tableHeaders)
            expect(customSort).to.eql(wrapper.vm.customSort)
        })

        it(`Should conditionally render stage-footer`, () => {
            wrapper = mountFactory({ computed: { isRunning: () => true } })
            const selector = '[data-test="stage-footer"]'
            expect(wrapper.find(selector).exists()).to.be.false
            wrapper = mountFactory({ computed: { isRunning: () => false } })
            expect(wrapper.find(selector).exists()).to.be.true
        })

        it(`Should render active item error in output message container`, () => {
            const activeItemStub = {
                create: 'create script',
                insert: 'insert script',
                error: 'Failed to create table',
                schema: 'test',
                select: 'select script',
                table: 't1',
                id: '843c8540-5854-11ee-941f-f9def38fcff8',
            }
            wrapper = mountFactory({ data: () => ({ activeItem: activeItemStub }) })
            expect(wrapper.find('[data-test="output-msg-ctr"]').text()).to.equal(
                activeItemStub.error
            )
        })

        it(`Should render active item creation error in output message container`, () => {
            wrapper = mountFactory({
                data: () => ({
                    activeItem: {
                        create: 'create script',
                        insert: 'insert script',
                        schema: 'test',
                        select: 'select script',
                        table: 't1',
                        id: '843c8540-5854-11ee-941f-f9def38fcff8',
                    },
                }),
                computed: { hasErrAtCreationStage: () => true },
            })
            expect(wrapper.find('[data-test="output-msg-ctr"]').text()).to.equal(
                wrapper.vm.$mxs_t('warnings.objCreation')
            )
        })

        it(`Should conditionally render Start Migration button`, () => {
            wrapper = mountFactory()
            const selector = '[data-test="start-migration-btn"]'
            expect(wrapper.find(selector).exists()).to.be.false
            wrapper = mountFactory({
                computed: { isPrepareEtl: () => true, isOutputMsgShown: () => false },
            })
            expect(wrapper.find(selector).exists()).to.be.true
        })
    })

    describe('Computed tests', () => {
        const tableHeadersTestCases = [
            { isPrepareEtl: true, expectedMappedValues: ['schema', 'table'] },
            { isPrepareEtl: false, expectedMappedValues: ['obj', 'result'] },
        ]
        tableHeadersTestCases.forEach(({ isPrepareEtl, expectedMappedValues }) => {
            it(`tableHeaders should return expected mapped values when
            isPrepareEtl is ${isPrepareEtl}`, () => {
                wrapper = mountFactory({ computed: { isPrepareEtl: () => isPrepareEtl } })
                wrapper.vm.tableHeaders.forEach((h, i) =>
                    expect(h.value).to.equal(expectedMappedValues[i])
                )
            })
        })

        const booleanProperties = [
            'isRunning',
            'isInErrState',
            'isPrepareEtl',
            'hasErrAtCreationStage',
            'isOutputMsgShown',
        ]
        booleanProperties.forEach(property =>
            it(`${property} should return a boolean`, () => {
                wrapper = mountFactory()
                expect(wrapper.vm[property]).to.be.a('boolean')
            })
        )

        it(`isOutputMsgShown should return true when isPrepareEtl is false`, () => {
            wrapper = mountFactory({ computed: { isPrepareEtl: () => false } })
            expect(wrapper.vm.isOutputMsgShown).to.be.true
        })

        const prepareScriptInfoTestCases = [
            { isInErrState: true, expected: 'errors.failedToPrepareMigrationScript' },
            { isInErrState: false, expected: 'info.migrationScriptInfo' },
        ]
        prepareScriptInfoTestCases.forEach(({ isInErrState, expected }) =>
            it(`Should return expected string for prepareScriptInfo when
            isInErrState is ${isInErrState}`, () => {
                wrapper = mountFactory({
                    computed: { isInErrState: () => isInErrState },
                })
                expect(wrapper.vm.prepareScriptInfo).to.equal(wrapper.vm.$mxs_t(expected))
            })
        )
    })
    describe('Method tests', () => {
        afterEach(() => sinon.restore())

        it(`cancel method should dispatch cancelEtlTask`, () => {
            wrapper = mountFactory()
            const stub = sinon.stub(EtlTask, 'dispatch').resolves()
            wrapper.vm.cancel()
            stub.calledOnceWithExactly('cancelEtlTask', wrapper.vm.$props.task.id)
        })

        const objMigrationStatusTestCases = [
            {
                item: {},
                expected: { icon: ETL_STATUS.RUNNING, isSpinning: true, txt: '0 rows migrated' },
            },
            {
                item: { error: 'Some error message' },
                expected: { icon: ETL_STATUS.ERROR, isSpinning: false, txt: 'Error' },
            },
            {
                item: { execution_time: 5, rows: 20 },
                expected: { icon: ETL_STATUS.COMPLETE, isSpinning: false, txt: '20 rows migrated' },
            },
        ]
        objMigrationStatusTestCases.forEach(({ item, expected }) =>
            it(`objMigrationStatus should return expected value`, () => {
                wrapper = mountFactory({ computed: { isRunning: () => expected.isSpinning } })
                expect(wrapper.vm.objMigrationStatus(item)).to.eql(expected)
            })
        )

        it(`objMigrationStatus should return expected value
        when hasErrAtCreationStage is true`, () => {
            wrapper = mountFactory({ computed: { hasErrAtCreationStage: () => true } })
            expect(wrapper.vm.objMigrationStatus({ execution_time: 5, rows: 20 })).to.eql({
                icon: { value: '$vuetify.icons.mxs_alertWarning', color: 'warning' },
                isSpinning: false,
                txt: wrapper.vm.$mxs_t('warnings.objCreation'),
            })
        })

        const migratedItemStub = {
            schema: 'test',
            table: 't1',
            error: 'error text',
        }
        const customColTestCases = [
            { key: 'obj', expected: '`test`.`t1`' },
            { key: 'result', expected: 'Error' },
            { key: 'schema', expected: 'test' },
            { key: 'table', expected: 't1' },
            { key: 'unknown', expected: '' },
        ]
        customColTestCases.forEach(({ key, expected }) =>
            it(`objMigrationStatus should return expected value if key is ${key}`, () => {
                wrapper = mountFactory()
                expect(wrapper.vm.customCol(migratedItemStub, key)).to.eql(expected)
            })
        )

        it(`onRestart method should dispatch handleEtlCall`, () => {
            wrapper = mountFactory()
            const stub = sinon.stub(EtlTask, 'dispatch').resolves()
            wrapper.vm.onRestart('stubId')
            stub.calledOnceWithExactly('handleEtlCall', {
                id: 'stubId',
                tables: wrapper.vm.$data.stagingScript,
            })
        })

        it(`start method should be handler as expected`, () => {
            wrapper = mountFactory()
            const updateStub = sinon.stub(EtlTask, 'update')
            const dispatchStub = sinon.stub(EtlTask, 'dispatch').resolves()
            wrapper.vm.start()
            updateStub.should.have.been.calledOnce
            dispatchStub.calledOnceWithExactly('handleEtlCall', {
                id: wrapper.vm.$props.task.id,
                tables: wrapper.vm.$data.stagingScript,
            })
        })
    })
})
