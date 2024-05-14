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
import OperationsList from '@/components/details/OperationsList.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) => mount(OperationsList, lodash.merge({ shallow: false }, opts))

function checkComponentExistence(wrapper, component, exists) {
  expect(wrapper.findComponent({ name: component }).exists()).toBe(exists)
}

describe('OperationsList', () => {
  let wrapper

  const renderTestCases = [
    { component: 'VDivider', stubData: null, shouldExist: false },
    { component: 'VDivider', stubData: [{ divider: true }], shouldExist: true },
    { component: 'VListSubheader', stubData: null, shouldExist: false },
    { component: 'VListSubheader', stubData: [{ subheader: 'sub-header' }], shouldExist: true },
    { component: 'VListItem', stubData: [{ title: 'Operation 1' }], shouldExist: true },
  ]

  for (const { component, stubData, shouldExist } of renderTestCases) {
    it(`Should ${shouldExist ? '' : 'not '}render ${component} ${shouldExist ? 'when correspond data is provided' : 'initially'}`, async () => {
      wrapper = mountFactory()
      if (stubData) await wrapper.setProps({ data: [stubData] })
      checkComponentExistence(wrapper, component, shouldExist)
    })
  }

  it('Should pass disable property to VListItem', () => {
    wrapper = mountFactory({
      props: {
        data: [[{ title: 'Operation 1', disabled: true, icon: 'mxs:running', color: 'red' }]],
      },
    })
    expect(wrapper.findComponent({ name: 'VListItem' }).vm.$props.disabled).toBe(true)
  })

  it('Should call the handler function when VListItem is clicked', async () => {
    const mockHandler = vi.fn()
    const wrapper = mountFactory({
      props: { data: [[{ title: 'Operation 1' }]], handler: mockHandler },
    })
    const vListItem = wrapper.findComponent({ name: 'VListItem' })
    await vListItem.trigger('click')
    expect(mockHandler).toHaveBeenCalled()
  })
})
