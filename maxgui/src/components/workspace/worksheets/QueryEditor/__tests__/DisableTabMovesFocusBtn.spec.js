/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 *  Public License.
 */
import mount from '@/tests/mount'
import DisableTabMovesFocusBtn from '@wkeComps/QueryEditor/DisableTabMovesFocusBtn.vue'
import { createStore } from 'vuex'

async function mockShowingButton(wrapper) {
  wrapper.vm.isTabMoveFocus = true
  await wrapper.vm.$nextTick()
}

describe(`DisableTabMovesFocusBtn`, () => {
  let wrapper
  beforeEach(() => {
    wrapper = mount(
      DisableTabMovesFocusBtn,
      { shallow: false },
      createStore({
        state: {
          prefAndStorage: { tab_moves_focus: false },
        },
        mutations: {
          'prefAndStorage/SET_TAB_MOVES_FOCUS': (state, value) =>
            (state.prefAndStorage.tab_moves_focus = value),
        },
      })
    )
  })

  it('Should not render the button when isTabMoveFocus is false', () => {
    expect(wrapper.findComponent({ name: 'TooltipBtn' }).exists()).toBe(false)
  })

  it('Should render the button when isTabMoveFocus is true', async () => {
    await mockShowingButton(wrapper)
    expect(wrapper.findComponent({ name: 'TooltipBtn' }).exists()).toBe(true)
  })

  it('Should disable isTabMoveFocus when the button is clicked', async () => {
    await mockShowingButton(wrapper)
    await wrapper.find('button').trigger('click')
    expect(wrapper.vm.isTabMoveFocus).toBe(false)
  })
})
