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
import { find } from '@/tests/utils'
import MigrationStage from '@wkeComps/DataMigration/MigrationStage.vue'
import { lodash } from '@/utils/helpers'
import etlTaskService from '@wsServices/etlTaskService'
import EtlTask from '@wsModels/EtlTask'
import { ETL_STATUS_MAP } from '@/constants/workspace'

const mountFactory = (opts) =>
  mount(
    MigrationStage,
    lodash.merge({ shallow: false, props: { task: { id: 'id' }, srcConn: {} } }, opts)
  )

describe('MigrationStage', () => {
  let wrapper

  it(`Should render WizardStageCtr`, () => {
    wrapper = mountFactory()
    expect(wrapper.findComponent({ name: 'WizardStageCtr' }).exists()).toBe(true)
  })

  it(`Should render stage header title`, () => {
    wrapper = mountFactory()
    expect(find(wrapper, 'stage-header-title').text()).toBe(wrapper.vm.$t('migration'))
  })

  it(`Should pass expected data to MigrationManage`, () => {
    wrapper = mountFactory()
    expect(wrapper.findComponent({ name: 'MigrationManage' }).vm.$props.task).toStrictEqual(
      wrapper.vm.$props.task
    )
  })

  it(`Should conditionally show prepare script info`, async () => {
    wrapper = mountFactory()
    const selector = 'prepare-script-info'
    expect(find(wrapper, selector).exists()).toBe(false)
    await wrapper.setProps({
      task: { id: 'id', is_prepare_etl: true, status: ETL_STATUS_MAP.INITIALIZING },
    })
    expect(find(wrapper, selector).text()).toBe(wrapper.vm.prepareScriptInfo)
  })

  it(`Should conditionally render VProgressLinear`, async () => {
    wrapper = mountFactory()
    const selector = { name: 'VProgressLinear' }
    expect(wrapper.findComponent(selector).exists()).toBe(false)
    await wrapper.setProps({
      task: { id: 'id', is_prepare_etl: true, status: ETL_STATUS_MAP.RUNNING },
    })
    expect(wrapper.findComponent(selector).exists()).toBe(true)
  })

  it(`Should pass expected data to MigrationTblScript`, () => {
    wrapper = mountFactory()
    const {
      $props: { task, data },
      $attrs: { headers },
    } = wrapper.findComponent({ name: 'MigrationTblScript' }).vm
    expect(task).toStrictEqual(wrapper.vm.$props.task)
    expect(data).toStrictEqual(wrapper.vm.items)
    expect(headers).toStrictEqual(wrapper.vm.tableHeaders)
  })

  it(`Should conditionally render stage-footer`, async () => {
    wrapper = mountFactory({ props: { task: { id: 'id', status: ETL_STATUS_MAP.RUNNING } } })
    const selector = 'stage-footer'
    expect(find(wrapper, selector).exists()).toBe(false)
    await wrapper.setProps({
      task: { id: 'id', status: ETL_STATUS_MAP.INITIALIZING },
    })
    expect(find(wrapper, selector).exists()).toBe(true)
  })

  it(`Should render active item error in output message container`, async () => {
    const activeItemStub = {
      create: 'create script',
      insert: 'insert script',
      error: 'Failed to create table',
      schema: 'test',
      select: 'select script',
      table: 't1',
      id: '843c8540-5854-11ee-941f-f9def38fcff8',
    }
    wrapper = mountFactory()
    wrapper.vm.activeItem = activeItemStub
    await wrapper.vm.$nextTick()
    expect(find(wrapper, 'output-msg-ctr').text()).toBe(activeItemStub.error)
  })

  const tableHeadersTestCases = [
    { isPrepareEtl: true, expectedMappedValues: ['schema', 'table'] },
    { isPrepareEtl: false, expectedMappedValues: ['obj', 'result'] },
  ]
  tableHeadersTestCases.forEach(({ isPrepareEtl, expectedMappedValues }) => {
    it(`tableHeaders should return expected mapped values when
          isPrepareEtl is ${isPrepareEtl}`, () => {
      wrapper = mountFactory({
        props: { task: { id: 'id', is_prepare_etl: isPrepareEtl } },
      })
      wrapper.vm.tableHeaders.forEach((h, i) =>
        expect(h.value).toStrictEqual(expectedMappedValues[i])
      )
    })
  })

  it(`onRestart function should be handled as expected`, () => {
    wrapper = mountFactory()
    const spy = vi.spyOn(etlTaskService, 'handleEtlCall')
    wrapper.vm.onRestart('stubId')
    expect(spy).toHaveBeenCalledWith({ id: 'stubId', tables: wrapper.vm.stagingScript })
  })

  it(`start function should be handled as expected`, () => {
    wrapper = mountFactory()
    const updateSpy = vi.spyOn(EtlTask, 'update')
    const handleEtlCallSpy = vi.spyOn(etlTaskService, 'handleEtlCall')
    wrapper.vm.start()
    expect(updateSpy).toHaveBeenCalled()
    expect(handleEtlCallSpy).toHaveBeenCalledWith({
      id: wrapper.vm.$props.task.id,
      tables: wrapper.vm.stagingScript,
    })
  })
})
