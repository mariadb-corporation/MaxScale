/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import DataMigration from '@wkeComps/DataMigration/DataMigration.vue'
import EtlTask from '@wsModels/EtlTask'
import { lodash } from '@/utils/helpers'

const taskStub = { id: 'id', is_prepare_etl: false, active_stage_index: 0 }

const mountFactory = (opts) => mount(DataMigration, lodash.merge({ props: { taskId: 'id' } }, opts))

describe('DataMigration', () => {
  let wrapper

  vi.mock('@wsServices/etlTaskService', async (importOriginal) => ({
    default: {
      ...(await importOriginal),
      find: vi.fn(() => taskStub),
      findResTables: vi.fn(() => []),
    },
  }))

  afterEach(() => vi.clearAllMocks())

  it('Should have expected number of stages', () => {
    wrapper = mountFactory()
    expect(wrapper.vm.stages.length).toBe(4)
  })

  it(`Should return accurate value for activeStageIdx`, () => {
    wrapper = mountFactory()
    expect(wrapper.vm.activeStageIdx).toBe(taskStub.active_stage_index)
  })

  it(`Should call EtlTask.update`, async () => {
    wrapper = mountFactory()
    const spy = vi.spyOn(EtlTask, 'update')
    const newValue = taskStub.active_stage_index + 1
    wrapper.vm.activeStageIdx = newValue
    await wrapper.vm.$nextTick()
    expect(spy).toHaveBeenCalledWith({
      where: taskStub.id,
      data: { active_stage_index: newValue },
    })
  })
})
