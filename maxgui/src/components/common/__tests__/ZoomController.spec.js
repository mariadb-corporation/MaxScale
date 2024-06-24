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
import ZoomController from '@/components/common/ZoomController.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    ZoomController,
    lodash.merge(
      {
        props: {
          zoomRatio: 1,
          isFitIntoView: false,
        },
      },
      opts
    )
  )

describe('ZoomController', () => {
  let wrapper
  it('Render the correct zoom percentage', async () => {
    wrapper = mountFactory({ props: { zoomRatio: 0.5 }, shallow: false })
    const zoomDisplay = wrapper.find('.v-select__selection')
    expect(zoomDisplay.text()).toContain('50%')
  })

  it('Render the "Fit" option', async () => {
    wrapper = mountFactory({ props: { isFitIntoView: true }, shallow: false })
    const zoomDisplay = wrapper.find('.v-select__selection')
    expect(zoomDisplay.text()).toContain(wrapper.vm.$t('fit'))
  })
})
