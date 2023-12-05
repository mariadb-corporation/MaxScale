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
import EtlMigrationManage from '@wkeComps/DataMigration/EtlMigrationManage'
import { lodash } from '@share/utils/helpers'
import { task } from '@wkeComps/DataMigration/__tests__/stubData'
import { ETL_ACTIONS } from '@wsSrc/store/config'
import EtlTask from '@wsModels/EtlTask'

const mountFactory = opts =>
    mount(lodash.merge({ shallow: true, component: EtlMigrationManage, propsData: { task } }, opts))

describe('EtlMigrationManage', () => {
    let wrapper

    describe("Child component's data communication tests", () => {
        it(`Should render quick action button when shouldShowQuickActionBtn is true`, () => {
            wrapper = mountFactory({ computed: { shouldShowQuickActionBtn: () => true } })
            expect(wrapper.find('[data-test="quick-action-btn"]').exists()).to.be.true
        })

        it(`Should render etl-task-manage when shouldShowQuickActionBtn is false`, () => {
            wrapper = mountFactory({ computed: { shouldShowQuickActionBtn: () => false } })
            expect(wrapper.findComponent({ name: 'etl-task-manage' }).exists()).to.be.true
        })

        it(`Should pass accurate data to etl-task-manage`, () => {
            wrapper = mountFactory({ computed: { shouldShowQuickActionBtn: () => false } })
            const {
                $attrs: { value },
                $props: { task, types },
            } = wrapper.findComponent({ name: 'etl-task-manage' }).vm
            expect(value).to.equal(wrapper.vm.$data.isMenuOpened)
            expect(task).to.eql(wrapper.vm.$props.task)
            expect(types).to.eql(wrapper.vm.actionTypes)
        })
    })
    describe('Computed and method tests', () => {
        afterEach(() => sinon.restore())

        it(`actionTypes should be an array with expected strings`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.actionTypes).to.eql([
                ETL_ACTIONS.CANCEL,
                ETL_ACTIONS.DELETE,
                ETL_ACTIONS.DISCONNECT,
                ETL_ACTIONS.MIGR_OTHER_OBJS,
                ETL_ACTIONS.RESTART,
            ])
        })

        const booleanProperties = ['hasNoConn', 'isRunning', 'isDone', 'shouldShowQuickActionBtn']
        booleanProperties.forEach(property =>
            it(`${property} should return a boolean`, () => {
                wrapper = mountFactory()
                expect(wrapper.vm[property]).to.be.a('boolean')
            })
        )

        it(`quickActionBtnData should return an object with expected fields`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.quickActionBtnData).to.be.an('object')
            expect(wrapper.vm.quickActionBtnData).to.have.all.keys('type', 'txt')
        })

        it(`Should handle quickActionHandler method as expected`, () => {
            wrapper = mountFactory()
            const stub = sinon.stub(EtlTask, 'dispatch').resolves()
            wrapper.vm.quickActionHandler()
            stub.calledOnceWithExactly('actionHandler', {
                type: wrapper.vm.quickActionBtnData.type,
                task: wrapper.vm.$props.task,
            })
        })
    })
})
