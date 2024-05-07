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
import { find } from '@/tests/utils'
import ObjSelectStage from '@wkeComps/DataMigration/ObjSelectStage.vue'
import { lodash } from '@/utils/helpers'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import EtlTask from '@wsModels/EtlTask'
import { ETL_CREATE_MODES } from '@/constants/workspace'

const mountFactory = (opts) =>
  mount(ObjSelectStage, lodash.merge({ props: { task: { id: 'taskId' } } }, opts))

const fetchSrcSchemasMock = vi.hoisted(() => vi.fn())
const handleEtlCallMock = vi.hoisted(() => vi.fn())

vi.mock('@wsServices/etlTaskService', async (importOriginal) => ({
  default: {
    ...(await importOriginal),
    findSrcSchemaTree: vi.fn(() => []),
    findCreateMode: vi.fn(() => ETL_CREATE_MODES.NORMAL),
    fetchSrcSchemas: fetchSrcSchemasMock,
    handleEtlCall: handleEtlCallMock,
    findMigrationObjs: vi.fn(() => []),
  },
}))

describe('ObjSelectStage', () => {
  let wrapper

  beforeEach(() => (wrapper = mountFactory({ shallow: false })))
  afterEach(() => vi.clearAllMocks())

  it(`Should render WizardStageCtr`, () => {
    expect(wrapper.findComponent({ name: 'WizardStageCtr' }).exists()).toBe(true)
  })

  it(`Should render stage header title`, () => {
    expect(find(wrapper, 'stage-header-title').text()).toBe(wrapper.vm.$t('selectObjsToMigrate'))
  })

  it(`Should pass expected data to CreateModeInput`, () => {
    expect(wrapper.findComponent({ name: 'CreateModeInput' }).vm.$props.taskId).toBe(
      wrapper.vm.$props.task.id
    )
  })

  it(`Should pass expected data to VirSchemaTree`, () => {
    const {
      $props: { expandedNodes, selectedNodes, data, loadChildren },
      $attrs: { height },
    } = wrapper.findComponent({
      name: 'VirSchemaTree',
    }).vm
    expect(expandedNodes).toStrictEqual(wrapper.vm.expandedNodes)
    expect(selectedNodes).toStrictEqual(wrapper.vm.selectedObjs)
    expect(data).toStrictEqual(wrapper.vm.srcSchemaTree)
    expect(loadChildren).toStrictEqual(wrapper.vm.loadTables)
    expect(height).toBe('100%')
  })

  it(`Should pass expected data to MigrationLogs`, () => {
    expect(wrapper.findComponent({ name: 'MigrationLogs' }).vm.$props.task).toStrictEqual(
      wrapper.vm.$props.task
    )
  })

  it('Should render prepare migration script button', () => {
    const btn = find(wrapper, 'prepare-btn')
    expect(btn.exists()).toBe(true)
    expect(btn.vm.$props.disabled).toBe(wrapper.vm.disabled)
  })

  it(`categorizeObjs should return an object with expected fields`, () => {
    assert.containsAllKeys(wrapper.vm.categorizeObjs, ['emptySchemas', 'tables'])
  })

  it(`Should update migration_objs field in EtlTaskTmp`, async () => {
    const spy = vi.spyOn(EtlTaskTmp, 'update')
    wrapper.vm.selectedObjs = [
      { parentNameData: { SCHEMA: 'test' }, type: 'TABLE', name: 'QueryConn' },
    ]
    await wrapper.vm.$nextTick()
    expect(spy).toHaveBeenCalledWith({
      where: wrapper.vm.$props.task.id,
      data: { migration_objs: wrapper.vm.tables },
    })
  })

  it(`Should dispatch fetchSrcSchemas`, () => {
    wrapper = mountFactory()
    expect(fetchSrcSchemasMock).toHaveBeenCalled()
  })

  it(`Should handle next method as expected`, async () => {
    wrapper = mountFactory()
    const spy = vi.spyOn(EtlTask, 'update')
    await wrapper.vm.next()
    expect(spy).toHaveBeenCalled()
    expect(handleEtlCallMock).toHaveBeenCalledWith({
      id: wrapper.vm.$props.task.id,
      tables: wrapper.vm.tables,
    })
  })
})
