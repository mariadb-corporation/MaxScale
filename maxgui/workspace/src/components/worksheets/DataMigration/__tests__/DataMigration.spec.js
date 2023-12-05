/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import DataMigration from '@wkeComps/DataMigration'
import EtlTask from '@wsModels/EtlTask'
import { lodash } from '@share/utils/helpers'
import { ETL_STATUS } from '@wsSrc/store/config'
import { task } from '@wkeComps/DataMigration/__tests__/stubData'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: DataMigration,
                propsData: { taskId: task.id },
                computed: {
                    task: () => task,
                },
            },
            opts
        )
    )

describe('DataMigration', () => {
    let wrapper

    afterEach(() => sinon.restore())

    it('Should have expected number of stages', () => {
        wrapper = mountFactory()
        expect(wrapper.vm.stages.length).to.equal(4)
    })

    const stageComponentNames = [
        'etl-overview-stage',
        'etl-conns-stage',
        'etl-obj-select-stage',
        'etl-migration-stage',
    ]
    stageComponentNames.forEach((name, i) => {
        it(`Should render and pass data to ${name}`, () => {
            wrapper = mountFactory({ computed: { activeStageIdx: () => i } })
            const component = wrapper.findComponent({ name })
            expect(component.exists()).to.be.true
            const { task, hasConns, srcConn, destConn } = component.vm.$props
            expect(task).to.eql(wrapper.vm.task)
            switch (name) {
                case 'etl-overview-stage':
                case 'etl-conns-stage':
                    expect(hasConns).to.equal(wrapper.vm.hasConns)
                    if (name !== 'etl-overview-stage') {
                        expect(srcConn).to.equal(wrapper.vm.srcConn)
                        expect(destConn).to.equal(wrapper.vm.destConn)
                    }
                    break
                case 'etl-migration-stage':
                    expect(srcConn).to.equal(wrapper.vm.srcConn)
                    break
            }
        })
    })

    it('Should disable migration stage if either src or dest connection is expired', () => {
        // mock isPreparingEtl and hasConns
        wrapper = mountFactory({
            computed: { isPreparingEtl: () => true, hasConns: () => false },
        })
        expect(wrapper.vm.isMigrationDisabled).to.be.true
    })

    it(`Should disable migration stage if it's still getting etl result`, () => {
        wrapper = mountFactory({ computed: { hasEtlRes: () => false } })
        expect(wrapper.vm.isMigrationDisabled).to.be.true
    })

    const connsAliveTestCases = [true, false]
    connsAliveTestCases.forEach(value => {
        it(`Should ${value ? '' : 'not '}disable the connection stage
        when hasConns is ${value}`, () => {
            wrapper = mountFactory({ computed: { hasConns: () => value } })
            const stage = wrapper.vm.stages.find(stage => stage.component === 'etl-conns-stage')
            expect(stage.isDisabled).to.be[value]
        })

        it(`Should ${value ? 'not ' : ''}disable the objects selection stage
        when hasConns is ${value}`, () => {
            wrapper = mountFactory({ computed: { hasConns: () => value } })
            const stage = wrapper.vm.stages.find(
                stage => stage.component === 'etl-obj-select-stage'
            )
            expect(stage.isDisabled).to.be[!value]
        })
    })

    const taskStatusTestCases = [ETL_STATUS.COMPLETE, ETL_STATUS.RUNNING]
    taskStatusTestCases.forEach(status => {
        it(`Should disable the connection stage when task status is ${status}`, () => {
            wrapper = mountFactory({
                computed: { hasConns: () => false, task: () => ({ ...task, status }) },
            })
            const stage = wrapper.vm.stages.find(stage => stage.component === 'etl-conns-stage')
            expect(stage.isDisabled).to.be.true
        })

        it(`Should disable the objects selection stage when task status is ${status}`, () => {
            wrapper = mountFactory({
                computed: { hasConns: () => true, task: () => ({ ...task, status }) },
            })
            const stage = wrapper.vm.stages.find(stage => stage.component === 'etl-conns-stage')
            expect(stage.isDisabled).to.be.true
        })
    })

    it(`Should return accurate value for activeStageIdx`, () => {
        wrapper = mountFactory()
        expect(wrapper.vm.activeStageIdx).to.equal(task.active_stage_index)
    })

    it(`Should call EtlTask.update`, () => {
        wrapper = mountFactory()
        const stub = sinon.stub(EtlTask, 'update')
        const newValue = task.active_stage_index + 1
        wrapper.vm.activeStageIdx = newValue
        stub.should.have.been.calledOnceWithExactly({
            where: task.id,
            data: { active_stage_index: newValue },
        })
    })
})
