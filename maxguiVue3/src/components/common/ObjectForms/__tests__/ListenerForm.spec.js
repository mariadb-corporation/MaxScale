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
import ListenerForm from '@/components/common/ObjectForms/ListenerForm.vue'
import { MXS_OBJ_TYPES, MRDB_PROTOCOL } from '@/constants'

const modulesMockData = [
  {
    attributes: {
      module_type: 'Protocol',
      parameters: [
        {
          mandatory: true,
          name: 'protocol',
          type: 'string',
          default_value: 'mariadbclient',
          disabled: true,
        },
        { mandatory: false, name: 'socket', type: 'string' },
      ],
    },
    id: 'mariadbclient',
  },
]

const allServicesMock = [{ id: 'test-service', type: 'services', attributes: {} }]
describe('ListenerForm.vue', () => {
  let wrapper
  beforeEach(() => {
    wrapper = mount(ListenerForm, {
      shallow: false,
      props: {
        allServices: allServicesMock,
        moduleParamsProps: { modules: modulesMockData, validate: () => null },
      },
    })
  })

  it(`Should pass expected data to ModuleParameters`, () => {
    const moduleParameters = wrapper.findComponent({ name: 'ModuleParameters' })
    const {
      $props: { moduleName, modules, validate, defModuleId },
      $attrs: { mxsObjType },
    } = moduleParameters.vm

    expect(moduleName).toBe('protocol')
    expect(modules).toStrictEqual(wrapper.vm.$props.moduleParamsProps.modules)
    assert.isFunction(validate)
    expect(defModuleId).toBe(MRDB_PROTOCOL)
    expect(mxsObjType).toBe(MXS_OBJ_TYPES.LISTENERS)
  })

  it(`Should compute servicesList as expected`, () => {
    expect(wrapper.vm.servicesList).to.be.an('array')
    wrapper.vm.servicesList.forEach((obj) => {
      expect(obj).to.have.all.keys('id', 'type')
    })
  })

  it(`Should pass expected data ResourceRelationships`, () => {
    const resourceRelationships = wrapper.findComponent({ name: 'ResourceRelationships' })
    const { type, items, initialValue, required } = resourceRelationships.vm.$attrs
    expect(type).toBe('services')
    expect(items).toStrictEqual(wrapper.vm.servicesList)
    expect(initialValue).toStrictEqual(wrapper.vm.$props.defaultItems)
    expect(required).toBeDefined
  })

  it(`getValues method should return expected values`, async () => {
    const mockChangedParams = { address: '127.0.0.1' }

    const moduleParameters = wrapper.findComponent({ name: 'ModuleParameters' })
    await moduleParameters.vm.emit('get-module-id', 'mockModuleId')
    await moduleParameters.vm.emit('get-changed-params', mockChangedParams)

    const resourceRelationships = wrapper.findComponent({ name: 'ResourceRelationships' })
    await resourceRelationships.vm.emit('get-values', wrapper.vm.servicesList)

    expect(wrapper.vm.getValues()).toStrictEqual({
      parameters: { ...mockChangedParams, protocol: 'mockModuleId' },
      relationships: {
        services: { data: wrapper.vm.servicesList },
      },
    })
  })
})
