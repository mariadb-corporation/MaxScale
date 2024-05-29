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
import EtlStatusIcon from '@wkeComps/DataMigration/EtlStatusIcon.vue'
import { ETL_STATUS } from '@/constants/workspace'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(EtlStatusIcon, lodash.merge({ props: { icon: '', spinning: false } }, opts))

describe('EtlStatusIcon', () => {
  let wrapper

  it('Should only render icon props has value', async () => {
    wrapper = mountFactory()
    expect(wrapper.findComponent({ name: 'VIcon' }).exists()).toBe(false)
    await wrapper.setProps({ icon: ETL_STATUS.RUNNING })
    expect(wrapper.findComponent({ name: 'VIcon' }).exists()).toBe(true)
  })

  it('Should pass expected data to VIcon', () => {
    wrapper = mountFactory({ props: { icon: ETL_STATUS.RUNNING } })
    const { size, color } = wrapper.findComponent({ name: 'VIcon' }).vm.$props
    expect(size).toBe('14')
    expect(color).toBe(wrapper.vm.data.semanticColor)
  })

  const iconTestCases = [
    {
      icon: ETL_STATUS.RUNNING,
      expectedValue: 'mxs:loading',
      expectedColorClass: 'navigation',
    },
    {
      icon: ETL_STATUS.CANCELED,
      expectedValue: 'mxs:critical',
      expectedColorClass: 'warning',
    },
    {
      icon: ETL_STATUS.ERROR,
      expectedValue: 'mxs:alertError',
      expectedColorClass: 'error',
    },
    {
      icon: ETL_STATUS.COMPLETE,
      expectedValue: 'mxs:good',
      expectedColorClass: 'success',
    },
  ]
  iconTestCases.forEach(({ icon, expectedValue, expectedColorClass }) => {
    it('Should return expected value for data computed property', () => {
      wrapper = mountFactory({ props: { icon } })
      expect(wrapper.vm.data).toStrictEqual({
        value: expectedValue,
        semanticColor: expectedColorClass,
      })
    })
  })

  it('data computed property should return custom icon object if it is defined', () => {
    const customIcon = { value: '$mdiKey', color: 'navigation' }
    wrapper = mountFactory({ props: { icon: customIcon } })
    expect(wrapper.vm.data).toStrictEqual(customIcon)
  })
})
