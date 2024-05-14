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
import DurationInput from '@/components/details/DurationInput.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) => mount(DurationInput, lodash.merge({}, opts))

describe('DurationInput', () => {
  let wrapper

  it('Should pass expected data to ParameterInput', () => {
    wrapper = mountFactory({ shallow: false, props: { modelValue: '1m' } })
    const { item, keyInfo, creationMode, isListener } = wrapper.findComponent({
      name: 'ParameterInput',
    }).vm.$props
    expect(item).toStrictEqual(wrapper.vm.item)
    expect(keyInfo).toStrictEqual(wrapper.vm.keyInfo)
    expect(creationMode).toBe(true)
    expect(isListener).toBe(false)
  })

  it('Should emit update:modelValue event when input value changes', async () => {
    wrapper = mountFactory({ shallow: false, props: { modelValue: '1m' } })
    const inputElement = wrapper.find('input')
    await inputElement.setValue('2m')
    expect(wrapper.emitted()['update:modelValue'][0][0]).toBe('2m')
  })
})
