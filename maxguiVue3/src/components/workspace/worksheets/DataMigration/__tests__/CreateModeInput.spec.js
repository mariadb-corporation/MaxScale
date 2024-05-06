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
import CreateModeInput from '@wkeComps/DataMigration/CreateModeInput.vue'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import { ETL_CREATE_MODES } from '@/constants/workspace'

describe('CreateModeInput', () => {
  let wrapper

  vi.mock('@wsServices/etlTaskService', async (importOriginal) => ({
    default: {
      ...(await importOriginal),
      findCreateMode: vi.fn(() => ETL_CREATE_MODES.NORMAL),
    },
  }))

  beforeEach(() => {
    wrapper = mount(CreateModeInput, { shallow: false, props: { taskId: '' } })
  })

  afterEach(() => vi.clearAllMocks())

  it('Should pass expected data to VSelect', () => {
    const { modelValue, items, itemTitle, itemValue, hideDetails, density } = wrapper.findComponent(
      { name: 'VSelect' }
    ).vm.$props
    expect(modelValue).toBe(wrapper.vm.createMode)
    expect(items).toStrictEqual(Object.values(ETL_CREATE_MODES))
    expect(itemTitle).toBe('text')
    expect(itemValue).toBe('id')
    expect(hideDetails).toBe(true)
    expect(density).toBe('compact')
  })

  it('Should update EtlTaskTmp when createMode is changed', async () => {
    const spy = vi.spyOn(EtlTaskTmp, 'update')
    const newValue = ETL_CREATE_MODES.REPLACE
    wrapper.vm.createMode = newValue
    await wrapper.vm.$nextTick()
    expect(spy).toHaveBeenCalledWith({
      where: wrapper.vm.$props.taskId,
      data: { create_mode: newValue },
    })
  })
})
