/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import FilterForm from '@/components/common/ObjectForms/FilterForm.vue'
import { MXS_OBJ_TYPES } from '@/constants'

const modulesMockData = [
  {
    attributes: {
      module_type: 'Filter',
      parameters: [{ mandatory: true, name: 'filebase', type: 'string' }],
    },
    id: 'qlafilter',
  },

  {
    attributes: {
      module_type: 'Filter',
      parameters: [],
    },
    id: 'hintfilter',
  },
]
const moduleParamsProps = { modules: modulesMockData, validate: vi.fn() }
describe('FilterForm.vue', () => {
  let wrapper
  beforeEach(() => {
    wrapper = mount(FilterForm, {
      shallow: false,
      props: { moduleParamsProps },
    })
  })

  it(`Should pass expected data to ModuleParameters`, () => {
    const moduleParameters = wrapper.findComponent({ name: 'ModuleParameters' })
    const {
      $props: { moduleName, modules, validate },
      $attrs: { mxsObjType },
    } = moduleParameters.vm
    expect(moduleName).toBe('module')
    expect(modules).toStrictEqual(wrapper.vm.$props.moduleParamsProps.modules)
    expect(mxsObjType).toBe(MXS_OBJ_TYPES.FILTERS)
    assert.isFunction(validate)
  })

  it(`getValues method should return expected values`, async () => {
    const mockChangedParams = { address: '127.0.0.1' }
    const moduleParameters = wrapper.findComponent({ name: 'ModuleParameters' })
    await moduleParameters.vm.emit('get-module-id', 'mockModuleId')
    await moduleParameters.vm.emit('get-changed-params', mockChangedParams)
    expect(wrapper.vm.getValues()).toStrictEqual({
      moduleId: 'mockModuleId',
      parameters: mockChangedParams,
    })
  })
})
