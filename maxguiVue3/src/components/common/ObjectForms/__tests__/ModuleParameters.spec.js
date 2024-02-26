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
import ModuleParameters from '@/components/common/ObjectForms/ModuleParameters.vue'

const mockupModules = [
  {
    attributes: {
      module_type: 'Router',
      parameters: [
        { mandatory: true, name: 'password' },
        { mandatory: false, name: 'delayed_retry' },
      ],
    },
    id: 'readwritesplit',
  },
  {
    attributes: {
      module_type: 'Router',
      parameters: [
        { mandatory: true, name: 'password' },
        { mandatory: false, name: 'log_auth_warnings' },
      ],
    },
    id: 'readconnroute',
  },
]
const moduleName = 'router'
const mockProps = {
  modules: mockupModules,
  moduleName,
  validate: vi.fn(),
}
const mockAttrs = { mxsObjType: 'services' }
describe('ModuleParameters', () => {
  let wrapper
  beforeEach(() => {
    wrapper = mount(ModuleParameters, {
      shallow: false,
      props: mockProps,
      attrs: mockAttrs,
    })
  })

  it(`Should render module name as input label accurately`, () => {
    expect(wrapper.find('[data-test="label"]').text()).toBe(moduleName)
  })

  it(`Should compute paramsObj as expected`, async () => {
    wrapper.vm.selectedModule = mockupModules[1]
    expect(wrapper.vm.paramsObj).toStrictEqual({
      password: undefined,
      log_auth_warnings: undefined,
    })
  })

  it(`Should use defModuleId as the default module`, async () => {
    await wrapper.setProps({ defModuleId: mockupModules.at(-1).id })
    expect(wrapper.vm.selectedModule).to.toStrictEqual(mockupModules.at(-1))
  })
  it(`Should hide module options dropdown`, async () => {
    await wrapper.setProps({ hideModuleOpts: true })
    expect(wrapper.find('[data-test="label"]').exists()).to.be.false
    expect(wrapper.find('#module-select').exists()).to.be.false
  })
})
