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
import ConnectionBtn from '@wsComps/ConnectionBtn.vue'

describe('ConnectionBtn', () => {
  let wrapper

  describe('When has active connection', () => {
    const activeConnStub = { meta: { name: 'server_0' } }
    beforeEach(() => (wrapper = mount(ConnectionBtn, { props: { activeConn: activeConnStub } })))

    it(`connectedServerName should return connection name from meta object properly`, () => {
      expect(wrapper.vm.connectedServerName).toBe(activeConnStub.meta.name)
    })

    it(`Should pass expected data to TooltipBtn`, () => {
      expect(wrapper.findComponent({ name: 'TooltipBtn' }).vm.$props.tooltipProps).toStrictEqual({
        disabled: !activeConnStub.meta.name,
      })
    })
  })

  describe('When there is no active connection', () => {
    beforeEach(() => (wrapper = mount(ConnectionBtn, { props: { activeConn: {} } })))

    it(`btnTxt should return a "Connect" string when connectedServerName is empty`, () => {
      expect(wrapper.vm.btnTxt).toBe(wrapper.vm.$t('connect'))
    })

    it(`Should disable the tooltip when connectedServerName is an empty string`, () => {
      expect(wrapper.findComponent({ name: 'TooltipBtn' }).vm.$props.tooltipProps).toStrictEqual({
        disabled: true,
      })
    })
  })
})
