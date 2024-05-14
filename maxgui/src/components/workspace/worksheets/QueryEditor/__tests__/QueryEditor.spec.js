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
 *  Public License.
 */
import mount from '@/tests/mount'
import QueryEditor from '@wkeComps/QueryEditor/QueryEditor.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts, storeMock) =>
  mount(
    QueryEditor,
    lodash.merge(
      { shallow: false, props: { ctrDim: { width: 1280, height: 800 }, queryEditorId: 'id' } },
      opts
    ),
    storeMock
  )

describe('QueryEditor', () => {
  let wrapper
  beforeEach(() => (wrapper = mountFactory()))

  it(`Should pass expected data to ResizablePanels`, () => {
    const { modelValue, boundary, minPercent, deactivatedMinPctZone, maxPercent, split, progress } =
      wrapper.findComponent({ name: 'ResizablePanels' }).vm.$props
    expect(modelValue).toBe(wrapper.vm.sidebarPct)
    expect(boundary).toBe(wrapper.vm.$props.ctrDim.width)
    expect(minPercent).toBe(wrapper.vm.minSidebarPct)
    expect(deactivatedMinPctZone).toBe(wrapper.vm.deactivatedMinSizeBarPoint)
    expect(maxPercent).toBe(wrapper.vm.maxSidebarPct)
    expect(split).toBe('vert')
    expect(progress).toBe(true)
  })

  it(`Should pass accurate data to SidebarCtr via props`, () => {
    const { queryEditorId, queryEditorTmp, activeQueryTabId, activeQueryTabConn, height } =
      wrapper.findComponent({ name: 'SidebarCtr' }).vm.$props
    expect(queryEditorId).toBe(wrapper.vm.$props.queryEditorId)
    expect(queryEditorTmp).toStrictEqual(wrapper.vm.queryEditorTmp)
    expect(activeQueryTabId).toBe(wrapper.vm.activeQueryTabId)
    expect(activeQueryTabConn).toStrictEqual(wrapper.vm.activeQueryTabConn)
    expect(height).toBe(wrapper.vm.$props.ctrDim.height)
  })

  it(`Should pass accurate data to QueryTabNavCtr via props`, () => {
    const { queryEditorId, activeQueryTabId, activeQueryTabConn, queryTabs, height } =
      wrapper.findComponent({ name: 'QueryTabNavCtr' }).vm.$props
    expect(queryEditorId).toBe(wrapper.vm.$props.queryEditorId)
    expect(queryTabs).toStrictEqual(wrapper.vm.queryTabs)
    expect(activeQueryTabId).toBe(wrapper.vm.activeQueryTabId)
    expect(activeQueryTabConn).toStrictEqual(wrapper.vm.activeQueryTabConn)
    expect(height).toBe(wrapper.vm.QUERY_TAB_CTR_HEIGHT)
  })
})
