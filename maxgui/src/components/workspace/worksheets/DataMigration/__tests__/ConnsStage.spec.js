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
import ConnsStage from '@wkeComps/DataMigration/ConnsStage.vue'
import { lodash } from '@/utils/helpers'

const taskStub = { id: 'id' }

const mountFactory = (opts) =>
  mount(
    ConnsStage,
    lodash.merge({ props: { task: taskStub, srcConn: {}, destConn: {}, hasConns: false } }, opts)
  )

describe('ConnsStage', () => {
  let wrapper
  beforeEach(() => (wrapper = mountFactory({ shallow: false })))

  it(`Should render WizardStageCtr`, () => {
    expect(wrapper.findComponent({ name: 'WizardStageCtr' }).exists()).toBe(true)
  })

  it(`Should pass accurate data to OdbcInputs`, () => {
    const { drivers } = wrapper.findComponent({ name: 'OdbcInputs' }).vm.$props
    expect(drivers).toStrictEqual(wrapper.vm.drivers)
  })

  it(`Should pass accurate data to DestConnInputs`, () => {
    const { items, type } = wrapper.findComponent({
      name: 'DestConnInputs',
    }).vm.$props
    expect(items).toStrictEqual(wrapper.vm.allServers)
    expect(type).toBe(wrapper.vm.DEST_TARGET_TYPE)
  })

  it(`Should pass accurate data to MigrationLogs`, () => {
    const { task } = wrapper.findComponent({ name: 'MigrationLogs' }).vm.$props
    expect(task).toStrictEqual(wrapper.vm.$props.task)
  })
})
