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
import HorizOperationsList from '@/components/details/HorizOperationsList.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) => mount(HorizOperationsList, lodash.merge({}, opts))

describe('HorizOperationsList', () => {
  let wrapper
  const stubData = [
    [{ title: 'Operation 1', icon: 'example-icon', color: 'red' }],
    [{ title: 'Operation 2', icon: 'example-icon', color: 'blue' }],
  ]
  it('Should render correct number of IconGroupWrapper components', async () => {
    wrapper = mountFactory({ props: { data: stubData } })
    const iconGroupWrappers = wrapper.findAllComponents({ name: 'IconGroupWrapper' })
    expect(iconGroupWrappers.length).toBe(stubData.length)
  })

  it('Should call the handler function when TooltipBtn is clicked', async () => {
    const mockHandler = vi.fn()
    wrapper = mountFactory({
      shallow: false,
      props: { data: stubData, handler: mockHandler },
    })
    /**
     * Since TooltipBtn component is a wrapper around a VBtn component
     * and provides a slot for the button content, so the click event should be
     * triggered on the VBtn component inside the TooltipBtn
     */
    await wrapper.findComponent({ name: 'VBtn' }).trigger('click')
    expect(mockHandler).toHaveBeenCalled()
  })
})
