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
import MonitorViewHeader from '@/components/details/MonitorViewHeader.vue'
import { MXS_OBJ_TYPES } from '@/constants'

const stubMonitor = {
  attributes: { module: 'MariadbMon' },
}

describe('MonitorViewHeader', () => {
  let wrapper

  beforeEach(() => {
    wrapper = mount(MonitorViewHeader, {
      shallow: false,
      props: { item: stubMonitor, successCb: vi.fn(), fetchCsStatus: vi.fn() },
    })
  })

  it(`Should render monitor module accurately`, () => {
    const span = wrapper.find('.resource-module')
    expect(span.exists()).toBe(true)
    expect(span.text()).toBe(stubMonitor.attributes.module)
  })

  it(`Should pass expected props to ViewHeader`, () => {
    const {
      item,
      type,
      showStateIcon,
      stateLabel,
      operationMatrix,
      onConfirm,
      onCountDone,
      defFormType,
      onConfirmDlgOpened,
      horizOperationList,
    } = wrapper.findComponent({ name: 'ViewHeader' }).vm.$props
    expect(item).toStrictEqual(wrapper.vm.$props.item)
    expect(type).toBe(MXS_OBJ_TYPES.MONITORS)
    expect(showStateIcon).toBe(true)
    expect(stateLabel).toBe(wrapper.vm.state)
    expect(operationMatrix).toStrictEqual(wrapper.vm.operationMatrix)
    expect(onConfirm).toStrictEqual(wrapper.vm.onConfirmOp)
    expect(onCountDone).toStrictEqual(wrapper.vm.$props.onCountDone)
    expect(defFormType).toBe(MXS_OBJ_TYPES.SERVERS)
    expect(onConfirmDlgOpened).toBe(wrapper.vm.onConfirmDlgOpened)
    expect(horizOperationList).toBe(false)
  })

  it('Should render module text', () => {
    expect(find(wrapper, 'module-text').text()).toBe(stubMonitor.attributes.module)
  })
})
