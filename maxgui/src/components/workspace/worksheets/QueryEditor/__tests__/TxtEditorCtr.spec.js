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
import TxtEditorCtr from '@wkeComps/QueryEditor/TxtEditorCtr.vue'
import TxtEditor from '@wsModels/TxtEditor'
import { queryTabStub } from '@/components/workspace/worksheets/QueryEditor/__tests__/stubData'
import { lodash } from '@/utils/helpers'
import { COMPACT_TOOLBAR_HEIGHT, KEYBOARD_SHORTCUT_MAP } from '@/constants/workspace'
import { useEventDispatcher } from '@/composables/common/common'

const propsStub = {
  dim: { width: 800, height: 600 },
  queryEditorTmp: { id: queryTabStub.query_editor_id },
  queryTab: queryTabStub,
}
const stubComponents = {
  TxtEditorToolbarCtr: true,
  SqlEditor: true,
  ChartConfig: true,
  ChartPane: true,
  QueryResultCtr: true,
}

const mountFactory = (opts = {}, store) =>
  mount(
    TxtEditorCtr,
    lodash.merge({ shallow: false, props: propsStub, global: { stubs: stubComponents } }, opts),
    store
  )

describe(`TxtEditorCtr`, () => {
  let wrapper

  vi.mock('@wsModels/TxtEditor', async (importOriginal) => {
    const OriginalEditor = await importOriginal()
    return {
      default: class extends OriginalEditor.default {
        static find = vi.fn(() => {})
      },
    }
  })

  vi.mock('@/composables/common/common', async (importOriginal) => ({
    ...(await importOriginal()),
    useEventDispatcher: vi.fn().mockReturnValue(vi.fn()),
  }))

  afterEach(() => {
    vi.clearAllMocks()
  })

  it.each`
    component
    ${'TxtEditorToolbarCtr'}
    ${'SqlEditor'}
    ${'QueryResultCtr'}
  `(`Should always render $component `, ({ component }) => {
    wrapper = mountFactory()
    expect(wrapper.findComponent({ name: component }).exists()).toBe(true)
  })

  it.each`
    case                           | isChartReady
    ${'render ChartPane when'}     | ${true}
    ${'not render ChartPane when'} | ${false}
  `(`Should $case when isChartReady is $isChartReady`, async ({ isChartReady }) => {
    wrapper = mountFactory()
    wrapper.vm.isChartReady = isChartReady
    await wrapper.vm.$nextTick()
    expect(wrapper.findComponent({ name: 'ChartPane' }).exists()).toBe(isChartReady)
  })

  it.each`
    case                             | isVisSidebarShown
    ${'render ChartConfig when'}     | ${true}
    ${'not render ChartConfig when'} | ${false}
  `(`Should $case when isVisSidebarShown is $isVisSidebarShown`, ({ isVisSidebarShown }) => {
    TxtEditor.find.mockReturnValue({ is_vis_sidebar_shown: isVisSidebarShown })
    wrapper = mountFactory()
    expect(wrapper.findComponent({ name: 'ChartConfig' }).exists()).toBe(isVisSidebarShown)
  })

  it.each`
    case               | isChartMaximized
    ${'its min value'} | ${true}
    ${'50%'}           | ${false}
  `(
    `Should set the width of the SqlEditor to $case when isChartMaximized is $isChartMaximized`,
    async ({ isChartMaximized }) => {
      wrapper = mountFactory()
      wrapper.vm.isChartReady = true
      wrapper.vm.isChartMaximized = isChartMaximized
      await wrapper.vm.$nextTick()
      expect(wrapper.vm.editorPanePctWidth).toBe(
        isChartMaximized ? wrapper.vm.editorPaneMinPctWidth : 50
      )
    }
  )

  it('Should pass expected data to TxtEditorToolbarCtr', () => {
    wrapper = mountFactory()
    const { height, queryTab, queryTabTmp, queryTabConn, sql, selectedSql, isVisSidebarShown } =
      wrapper.findComponent({ name: 'TxtEditorToolbarCtr' }).vm.$props
    expect(height).toBe(COMPACT_TOOLBAR_HEIGHT)
    expect(queryTab).toStrictEqual(queryTabStub)
    expect(queryTabTmp).toStrictEqual(wrapper.vm.queryTabTmp)
    expect(queryTabConn).toStrictEqual(wrapper.vm.queryTabConn)
    expect(sql).toBe(wrapper.vm.sql)
    expect(selectedSql).toBe(wrapper.vm.selectedSql)
    expect(isVisSidebarShown).toBe(wrapper.vm.isVisSidebarShown)
  })

  it('Should pass expected data to SqlEditor', () => {
    wrapper = mountFactory()
    const {
      modelValue,
      isTabMoveFocus,
      completionItems,
      isKeptAlive,
      customActions,
      supportCustomDelimiter,
    } = wrapper.findComponent({ name: 'SqlEditor' }).vm.$props
    expect(modelValue).toBe(wrapper.vm.sql)
    expect(isTabMoveFocus).toBe(wrapper.vm.tab_moves_focus)
    expect(completionItems).toStrictEqual(wrapper.vm.completionItems)
    expect(isKeptAlive).toBe(true)
    expect(customActions).toStrictEqual(wrapper.vm.EDITOR_ACTIONS)
    expect(supportCustomDelimiter).toBe(true)
  })

  it('Should pass expected data to ChartPane', async () => {
    wrapper = mountFactory()
    wrapper.vm.isChartReady = true
    await wrapper.vm.$nextTick()
    const { isMaximized, chartConfig, height } = wrapper.findComponent({ name: 'ChartPane' }).vm
      .$props
    expect(isMaximized).toBe(wrapper.vm.isChartMaximized)
    expect(chartConfig).toStrictEqual(wrapper.vm.chartConfig)
    expect(height).toBe(wrapper.vm.chartContainerHeight)
  })

  it('Should pass expected data to QueryResultCtr', () => {
    wrapper = mountFactory()
    const { dim, queryTab, queryTabConn, queryTabTmp, dataTableProps } = wrapper.findComponent({
      name: 'QueryResultCtr',
    }).vm.$props
    expect(dim).toStrictEqual(wrapper.vm.resultPaneDim)
    expect(queryTab).toStrictEqual(wrapper.vm.queryTab)
    expect(queryTabConn).toStrictEqual(wrapper.vm.queryTabConn)
    expect(queryTabTmp).toStrictEqual(wrapper.vm.queryTabTmp)
    expect(dataTableProps).toStrictEqual({
      placeToEditor: wrapper.vm.placeToEditor,
      onDragging: wrapper.vm.draggingTxt,
      onDragend: wrapper.vm.dropTxtToEditor,
    })
  })

  it('Should pass expected data to ChartConfig', () => {
    TxtEditor.find.mockReturnValue({ is_vis_sidebar_shown: true })
    wrapper = mountFactory()
    const { modelValue, resultSets } = wrapper.findComponent({
      name: 'ChartConfig',
    }).vm.$props
    expect(modelValue).toStrictEqual(wrapper.vm.chartConfig)
    expect(resultSets).toStrictEqual(wrapper.vm.resultSets)
  })

  it('Should update TxtEditor model when sql is changed', () => {
    const spy = vi.spyOn(TxtEditor, 'update')
    wrapper = mountFactory()
    const newSql = 'SELECT * FROM orders'
    wrapper.vm.sql = newSql
    expect(spy).toHaveBeenCalledWith({ where: queryTabStub.id, data: { sql: newSql } })
  })

  it('Should update selectedSql when SqlEditor emit on-selection event', async () => {
    wrapper = mountFactory()
    const sqlEditor = wrapper.findComponent({ name: 'SqlEditor' })
    const selectedSql = 'SELECT * FROM users'
    sqlEditor.vm.$emit('on-selection', selectedSql)
    expect(wrapper.vm.selectedSql).toBe(selectedSql)
  })

  it('Should call dispatchEvt when SqlEditor emit toggle-tab-focus-mode  event', () => {
    const dispatchEvtMock = vi.fn()
    useEventDispatcher.mockReturnValue(dispatchEvtMock)
    wrapper = mountFactory()
    const sqlEditor = wrapper.findComponent({ name: 'SqlEditor' })
    sqlEditor.vm.$emit('toggle-tab-focus-mode')
    expect(dispatchEvtMock).toHaveBeenCalledWith(KEYBOARD_SHORTCUT_MAP.CTRL_M)
  })

  it('Should call onCloseChart when ChartPane emit close-chart event', async () => {
    wrapper = mountFactory()
    const spy = vi.spyOn(wrapper.vm, 'onCloseChart')
    wrapper.vm.isChartReady = true
    await wrapper.vm.$nextTick()
    wrapper.findComponent({ name: 'ChartPane' }).vm.$emit('close-chart')
    expect(spy).toHaveBeenCalled()
  })

  it.each`
    expected
    ${true}
    ${false}
  `(
    `Should set isChartReady to $expected when ChartConfig emit is-chart-ready event`,
    async ({ expected }) => {
      TxtEditor.find.mockReturnValue({ is_vis_sidebar_shown: true })
      wrapper = mountFactory()
      wrapper.findComponent({ name: 'ChartConfig' }).vm.$emit('is-chart-ready', expected)
      expect(wrapper.vm.isChartReady).toBe(expected)
    }
  )

  it('Should expose placeToEditor, draggingTxt, and dropTxtToEditor', () => {
    // Mock a parent component
    const ParentComponent = {
      template: `<ChildComponent ref="childRef" v-bind="propsStub" />`,
      components: { ChildComponent: TxtEditorCtr },
      data: () => ({ propsStub }),
    }

    const wrapper = mount(ParentComponent, { shallow: false, global: { stubs: stubComponents } })
    const exposedFunctions = wrapper.vm.$refs.childRef

    expect(typeof exposedFunctions.placeToEditor).toBe('function')
    expect(typeof exposedFunctions.draggingTxt).toBe('function')
    expect(typeof exposedFunctions.dropTxtToEditor).toBe('function')
  })
})
