/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import EtlObjSelectStage from '@wkeComps/DataMigration/EtlObjSelectStage'
import { lodash } from '@share/utils/helpers'
import { task } from '@wkeComps/DataMigration/__tests__/stubData'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import EtlTask from '@wsModels/EtlTask'

const stubTblNode = {
    parentNameData: { SCHEMA: 'test' },
    type: 'TABLE',
    name: 'QueryConn',
}
const mountFactory = opts =>
    mount(lodash.merge({ shallow: true, component: EtlObjSelectStage, propsData: { task } }, opts))

describe('EtlObjSelectStage', () => {
    let wrapper

    afterEach(() => sinon.restore())

    describe("Child component's data communication tests", () => {
        it(`Should render mxs-stage-ctr`, () => {
            wrapper = mountFactory()
            expect(wrapper.findComponent({ name: 'mxs-stage-ctr' }).exists()).to.be.true
        })

        it(`Should render stage header title`, () => {
            wrapper = mountFactory()
            expect(wrapper.find('[data-test="stage-header-title"]').text()).to.equal(
                wrapper.vm.$mxs_t('selectObjsToMigrate')
            )
        })

        it(`Should pass accurate data to etl-create-mode-input`, () => {
            wrapper = mountFactory()
            expect(
                wrapper.findComponent({ name: 'etl-create-mode-input' }).vm.$props.taskId
            ).to.equal(wrapper.vm.$props.task.id)
        })

        it(`Should pass accurate data to v-treeview`, () => {
            wrapper = mountFactory({ computed: { srcSchemaTree: () => [] } })
            const {
                value,
                items,
                hoverable,
                dense,
                openOnClick,
                selectable,
                loadChildren,
                returnObject,
            } = wrapper.findComponent({ name: 'v-treeview' }).vm.$props
            expect(value).to.eql(wrapper.vm.$data.selectedObjs)
            expect(items).to.eql(wrapper.vm.srcSchemaTree)
            expect(hoverable).to.be.true
            expect(dense).to.be.true
            expect(openOnClick).to.be.true
            expect(selectable).to.be.true
            expect(loadChildren).to.eql(wrapper.vm.handleLoadChildren)
            expect(returnObject).to.be.true
        })

        it(`Should pass accurate data to etl-logs`, () => {
            wrapper = mountFactory()
            expect(wrapper.findComponent({ name: 'etl-logs' }).vm.$props.task).to.equal(
                wrapper.vm.$props.task
            )
        })
        const showConfirmTestCases = [true, false]
        showConfirmTestCases.forEach(v =>
            it(`Should${
                v ? '' : ' not'
            } render confirm checkbox when isReplaceMode is ${v}`, () => {
                wrapper = mountFactory({ computed: { isReplaceMode: () => v } })
                expect(wrapper.find('[data-test="confirm-checkbox"]').exists()).to.be[v]
            })
        )

        it('Should render prepare migration script button', () => {
            wrapper = mountFactory()
            const btn = wrapper.find('[data-test="prepare-btn"]')
            expect(btn.exists()).to.be.true
            expect(btn.vm.$props.disabled).to.equal(wrapper.vm.disabled)
        })
    })

    it(`categorizeObjs should return an object with expected fields`, () => {
        wrapper = mountFactory()
        expect(wrapper.vm.categorizeObjs).to.have.all.keys('emptySchemas', 'tables')
    })

    const booleanProperties = ['isReplaceMode', 'disabled', 'isLarge']
    booleanProperties.forEach(property =>
        it(`${property} should return a boolean`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm[property]).to.be.a('boolean')
        })
    )

    it(`Should update migration_objs field in EtlTaskTmp`, async () => {
        wrapper = mountFactory()
        const stub = sinon.stub(EtlTaskTmp, 'update')
        await wrapper.setData({ selectedObjs: [stubTblNode] })
        stub.calledOnceWithExactly({
            where: wrapper.vm.$props.task.id,
            data: { migration_objs: wrapper.vm.tables },
        })
    })

    it(`Should dispatch fetchSrcSchemas`, async () => {
        const stub = sinon.stub(EtlTask, 'dispatch')
        wrapper = mountFactory()
        stub.calledOnceWithExactly('fetchSrcSchemas')
    })
    it(`Should handle next method as expected`, () => {
        wrapper = mountFactory()
        const stubUpdate = sinon.stub(EtlTask, 'update')
        const stubDispatch = sinon.stub(EtlTask, 'dispatch').resolves()
        wrapper.vm.next()
        stubUpdate.calledOnce
        stubDispatch.calledOnceWithExactly('handleEtlCall', {
            id: wrapper.vm.$props.task.id,
            tables: wrapper.vm.tables,
        })
    })
})
