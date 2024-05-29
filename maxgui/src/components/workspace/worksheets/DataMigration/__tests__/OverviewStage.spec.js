/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import OverviewStage from '@wkeComps/DataMigration/OverviewStage.vue'
import { lodash } from '@/utils/helpers'
import { ETL_STATUS } from '@/constants/workspace'

const taskStub = { status: ETL_STATUS.INITIALIZING, id: 'id' }

const mountFactory = (opts) =>
  mount(OverviewStage, lodash.merge({ props: { task: taskStub, hasConns: false } }, opts))

describe('OverviewStage', () => {
  let wrapper

  it(`Should render WizardStageCtr`, () => {
    wrapper = mountFactory()
    expect(wrapper.findComponent({ name: 'WizardStageCtr' }).exists()).toBe(true)
  })

  const areConnsAliveTestCases = [true, false]
  areConnsAliveTestCases.forEach((value) => {
    it(`Should disable the Set Up Connections button when
        hasConns is ${value}`, () => {
      wrapper = mountFactory({ props: { hasConns: value } })
      expect(wrapper.vm.disabled).toBe(value)
    })
  })

  const taskStatusTestCases = [ETL_STATUS.COMPLETE, ETL_STATUS.RUNNING]
  taskStatusTestCases.forEach((status) => {
    it(`Should disable the Set Up Connections button when task status is ${status}`, () => {
      wrapper = mountFactory({
        props: { hasConns: false, task: { ...taskStub, status } },
      })
      expect(wrapper.vm.disabled).toBe(true)
    })
  })
})
