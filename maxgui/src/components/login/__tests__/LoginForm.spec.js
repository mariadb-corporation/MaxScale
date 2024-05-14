/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import LoginForm from '@/components/login/LoginForm.vue'
import { inputChangeMock, find } from '@/tests/utils'

function findUsernameInput(wrapper) {
  return find(wrapper, 'username-input')
}

function findPwdInput(wrapper) {
  return find(wrapper, 'pwd-input')
}

describe('LoginForm.vue', () => {
  let wrapper
  beforeEach(() => {
    wrapper = mount(LoginForm, { shallow: false })
  })

  it('Should render username and password fields.', () => {
    expect(findUsernameInput(wrapper).exists()).toBe(true)
    expect(findPwdInput(wrapper).exists()).toBe(true)
  })

  it('Should render login button.', () => {
    const loginBtn = find(wrapper, 'login-btn')
    expect(loginBtn.exists()).toBe(true)
  })

  it(`Should allows to toggle masked password`, async () => {
    const inputComponent = findPwdInput(wrapper)
    await inputChangeMock({ wrapper, value: 'mariadb', seletor: '[name="password"]' })
    expect(inputComponent.vm.$props.type).toBe('password')
    expect(wrapper.vm.isPwdVisible).toBe(false)
    const toggleBtn = find(wrapper, 'toggle-pwd-visibility-btn')
    expect(toggleBtn.exists()).toBe(true)
    await toggleBtn.trigger('click')
    expect(wrapper.vm.isPwdVisible).toBe(true)
    expect(inputComponent.vm.$props.type).toBe('text')
  })

  it('Should have rememberMe checked by default', () => {
    expect(wrapper.vm.rememberMe).toBe(true)
  })

  it('Should pass expect data to VCheckboxBtn', async () => {
    const { modelValue, label, density } = wrapper.findComponent({ name: 'VCheckboxBtn' }).vm.$props
    expect(modelValue).toBe(wrapper.vm.rememberMe)
    expect(label).toBe(wrapper.vm.$t('rememberMe'))
    expect(density).toBe('compact')
  })
})
