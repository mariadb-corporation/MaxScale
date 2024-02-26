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
import MonitorForm from '@/components/common/ObjectForms/MonitorForm.vue'
import { MXS_OBJ_TYPES, MRDB_MON } from '@/constants'

const modulesMockData = [
  {
    attributes: {
      module_type: 'Monitor',
      parameters: [
        {
          mandatory: true,
          name: 'user',
          type: 'string',
        },
        {
          mandatory: true,
          name: 'password',
          type: 'password',
        },
        {
          mandatory: false,
          name: 'detect_stale_slave',
          type: 'bool',
        },
      ],
    },
    id: 'mariadbmon',
  },
]
const allServersMock = [
  { id: 'server_0', type: 'servers', attributes: {} },
  { id: 'server_1', type: 'servers', attributes: {} },
]
const moduleParamsProps = { modules: modulesMockData, validate: vi.fn() }

describe('MonitorForm.vue', () => {
  let wrapper
  beforeEach(() => {
    wrapper = mount(MonitorForm, {
      shallow: false,
      props: {
        allServers: allServersMock,
        moduleParamsProps,
      },
    })
  })

  it(`Should pass expected data to ModuleParameters`, () => {
    const moduleParameters = wrapper.findComponent({ name: 'ModuleParameters' })
    const {
      $props: { moduleName, modules, defModuleId, validate },
      $attrs: { mxsObjType },
    } = moduleParameters.vm
    expect(moduleName).toBe('module')
    expect(defModuleId).toBe(MRDB_MON)
    expect(modules).toStrictEqual(wrapper.vm.$props.moduleParamsProps.modules)
    expect(mxsObjType).toBe(MXS_OBJ_TYPES.MONITORS)
    assert.isFunction(validate)
  })

  it(`Should pass expected data ResourceRelationships`, () => {
    const resourceRelationships = wrapper.findComponent({ name: 'ResourceRelationships' })
    const { type, multiple, initialValue, clearable, items } = resourceRelationships.vm.$attrs
    expect(type).toBe('servers')
    expect(items).toStrictEqual(wrapper.vm.serversList)
    expect(initialValue).toStrictEqual(wrapper.vm.defServers)
    expect(multiple).toBeDefined
    expect(clearable).toBeDefined
  })

  it(`getValues method should return expected values`, async () => {
    const mockChangedParams = { address: '127.0.0.1' }

    const moduleParameters = wrapper.findComponent({ name: 'ModuleParameters' })
    await moduleParameters.vm.emit('get-module-id', 'mockModuleId')
    await moduleParameters.vm.emit('get-changed-params', mockChangedParams)

    const resourceRelationships = wrapper.findComponent({ name: 'ResourceRelationships' })
    await resourceRelationships.vm.emit('get-values', wrapper.vm.serversList)

    expect(wrapper.vm.getValues()).toStrictEqual({
      moduleId: 'mockModuleId',
      parameters: mockChangedParams,
      relationships: {
        servers: { data: wrapper.vm.serversList },
      },
    })
  })
})
