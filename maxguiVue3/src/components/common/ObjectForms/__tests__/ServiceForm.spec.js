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
import ServiceForm from '@/components/common/ObjectForms/ServiceForm.vue'
import { MXS_OBJ_TYPES } from '@/constants'

const modulesMockData = [
  {
    attributes: {
      module_type: 'services',
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
      ],
    },
    id: 'readwritesplit',
  },
]
const moduleParamsProps = { modules: modulesMockData, validate: vi.fn() }

describe('ServiceForm', () => {
  let wrapper
  beforeEach(() => {
    wrapper = mount(ServiceForm, {
      shallow: false,
      props: {
        allFilters: [],
        moduleParamsProps,
      },
      global: { stubs: { RoutingTargetSelect: true } },
    })
  })

  it(`Should pass expected data to ModuleParameters`, () => {
    const moduleParameters = wrapper.findComponent({ name: 'ModuleParameters' })
    const {
      $props: { moduleName, modules, validate },
      $attrs: { mxsObjType },
    } = moduleParameters.vm
    expect(moduleName).toBe('router')
    expect(modules).toStrictEqual(wrapper.vm.$props.moduleParamsProps.modules)
    expect(mxsObjType).toBe(MXS_OBJ_TYPES.SERVICES)
    assert.isFunction(validate)
  })

  it(`Should pass expected data to  RoutingTargetSelect`, () => {
    const routingTargetSelect = wrapper.findComponent({ name: 'RoutingTargetSelect' })
    const { modelValue, initialValue } = routingTargetSelect.vm.$props
    expect(modelValue).toStrictEqual(wrapper.vm.routingTargetItems)
    expect(initialValue).to.be.deep.equals(wrapper.vm.$props.defRoutingTargetItems)
  })

  it(`Should pass expected data ResourceRelationships`, () => {
    const resourceRelationships = wrapper.findComponent({ name: 'ResourceRelationships' })
    const { type, items, initialValue, clearable, multiple } = resourceRelationships.vm.$attrs
    expect(type).toBe('filters')
    expect(items).toStrictEqual(wrapper.vm.filtersList)
    expect(initialValue).toStrictEqual(wrapper.vm.$props.defFilterItem)
    expect(clearable).toBeDefined
    expect(multiple).toBeDefined
  })

  it(`Should compute filtersList as expected`, () => {
    expect(wrapper.vm.filtersList).to.be.an('array')
    wrapper.vm.filtersList.forEach((obj) => {
      expect(obj).to.have.all.keys('id', 'type')
    })
  })

  it(`getValues method should return expected values`, async () => {
    const mockChangedParams = { address: '127.0.0.1' }

    const moduleParameters = wrapper.findComponent({ name: 'ModuleParameters' })
    await moduleParameters.vm.emit('get-module-id', 'mockModuleId')
    await moduleParameters.vm.emit('get-changed-params', mockChangedParams)

    const resourceRelationships = wrapper.findComponent({ name: 'ResourceRelationships' })
    await resourceRelationships.vm.emit('get-values', wrapper.vm.filtersList)

    expect(wrapper.vm.getValues()).toStrictEqual({
      moduleId: 'mockModuleId',
      parameters: mockChangedParams,
      relationships: { filters: { data: wrapper.vm.filtersList } },
    })
  })
})
