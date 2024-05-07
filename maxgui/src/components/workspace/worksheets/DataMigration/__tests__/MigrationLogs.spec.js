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
import MigrationLogs from '@wkeComps/DataMigration/MigrationLogs.vue'

const taskStub = { logs: { 1: [] }, active_stage_index: 1 }

describe('MigrationLogs', () => {
  let wrapper
  beforeEach(() => {
    wrapper = mount(MigrationLogs, { props: { task: taskStub } })
  })
  const properties = [
    {
      name: 'etlLog',
      datatype: 'object',
      expectedValue: taskStub.logs,
    },
    {
      name: 'activeStageIdx',
      datatype: 'number',
      expectedValue: taskStub.active_stage_index,
    },
    {
      name: 'logs',
      datatype: 'array',
      expectedValue: taskStub.logs[taskStub.active_stage_index] || [],
    },
  ]
  properties.forEach(({ name, datatype, expectedValue }) => {
    it(`Should return accurate data for ${name} computed property`, () => {
      if (datatype === 'array') expect(wrapper.vm[name]).toBeInstanceOf(Array)
      else expect(wrapper.vm[name]).toBeTypeOf(datatype)
      expect(wrapper.vm[name]).toStrictEqual(expectedValue)
    })
  })

  it(`Should render title`, () => {
    expect(find(wrapper, 'title').text()).toBe(wrapper.vm.$t('msgLog'))
  })

  it(`Should render log-time and log-txt`, async () => {
    await wrapper.setProps({
      task: {
        ...taskStub,
        logs: { 1: [{ name: 'test', timestamp: 169443682753 }] },
      },
    })
    expect(find(wrapper, 'log-time').exists()).toBe(true)
    expect(find(wrapper, 'log-txt').text()).toBe('test')
  })
})
