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
import { find } from '@/tests/utils'
import ErToolbar from '@wkeComps/ErdWke/ErToolbar.vue'
import workspace from '@/composables/workspace'
import { WS_KEY } from '@/constants/injectionKeys'
import { lodash } from '@/utils/helpers'
import { LINK_SHAPES } from '@/components/svgGraph/shapeConfig'
import { CONN_TYPE_MAP, KEYBOARD_SHORTCUT_MAP } from '@/constants/workspace'
import { createStore } from 'vuex'

const exportOptionsStub = [
  { title: 'copyScriptToClipboard', action: vi.fn },
  { title: 'exportScript', action: vi.fn },
]

const propsStub = {
  graphConfig: {
    linkShape: { type: LINK_SHAPES.ORTHO },
    link: { isAttrToAttr: false, isHighlightAll: false },
  },
  height: 200,
  zoom: 1,
  isFitIntoView: false,
  exportOptions: exportOptionsStub,
  conn: { id: 1 },
  nodesHistory: [{}, {}],
  activeHistoryIdx: 1,
}

const mountFactory = (opts, store) =>
  mount(
    ErToolbar,
    lodash.merge(
      {
        shallow: false,
        props: propsStub,
      },
      opts
    ),
    store
  )
const disableRedoBtnMockProps = { activeHistoryIdx: 4, nodesHistory: Array(5).fill({}) }
const enableRedoBtnMockProps = { activeHistoryIdx: 2, nodesHistory: Array(5).fill({}) }

describe(`ErToolbar`, () => {
  let wrapper, mockStore

  vi.mock('@/composables/workspace', async (importOriginal) => ({
    ...(await importOriginal()),
    useShortKeyListener: vi.fn(),
  }))

  beforeEach(() => {
    mockStore = createStore({
      mutations: { 'workspace/SET_GEN_ERD_DLG': vi.fn(), 'workspace/SET_CONN_DLG': vi.fn() },
    })
    mockStore.commit = vi.fn()
    wrapper = mountFactory({}, mockStore)
  })

  afterEach(() => vi.clearAllMocks())

  it('Should call SET_GEN_ERD_DLG with expected arg when genErd function is called', () => {
    wrapper.vm.genErd()
    expect(mockStore.commit).toHaveBeenCalledWith('workspace/SET_GEN_ERD_DLG', {
      is_opened: true,
      preselected_schemas: [],
      connection: wrapper.vm.$props.conn,
      gen_in_new_ws: false,
    })
  })

  it('Should call SET_CONN_DLG with expected arg when openCnnDlg function is called', () => {
    wrapper.vm.openCnnDlg()
    expect(mockStore.commit).toHaveBeenCalledWith('workspace/SET_CONN_DLG', {
      is_opened: true,
      type: CONN_TYPE_MAP.ERD,
    })
  })

  const { CTRL_SHIFT_ENTER, META_SHIFT_ENTER, CTRL_Z, META_Z, CTRL_SHIFT_Z, META_SHIFT_Z } =
    KEYBOARD_SHORTCUT_MAP

  it.each`
    key       | isUndoDisabled | expected
    ${CTRL_Z} | ${false}       | ${'emit on-undo'}
    ${META_Z} | ${false}       | ${'emit on-undo'}
    ${CTRL_Z} | ${true}        | ${'not emit anything'}
    ${META_Z} | ${true}        | ${'not emit anything'}
  `(
    'Should $expected when key $key is pressed and isUndoDisabled is $isUndoDisabled',
    async ({ key, isUndoDisabled }) => {
      await wrapper.setProps({ activeHistoryIdx: isUndoDisabled ? 0 : 1 }) // mock isUndoDisabled
      wrapper.vm.shortKeyHandler(key)
      if (isUndoDisabled) expect(wrapper.emitted('on-undo')).toBeFalsy()
      else expect(wrapper.emitted('on-undo')).toBeTruthy()
    }
  )

  it.each`
    key             | isRedoDisabled | expected
    ${CTRL_SHIFT_Z} | ${false}       | ${'emit on-redo'}
    ${META_SHIFT_Z} | ${false}       | ${'emit on-redo'}
    ${CTRL_SHIFT_Z} | ${true}        | ${'not emit anything'}
    ${META_SHIFT_Z} | ${true}        | ${'not emit anything'}
  `(
    'Should $expected when key $key is pressed and isRedoDisabled is $isRedoDisabled',
    async ({ key, isRedoDisabled }) => {
      await wrapper.setProps(isRedoDisabled ? disableRedoBtnMockProps : enableRedoBtnMockProps)
      wrapper.vm.shortKeyHandler(key)
      if (isRedoDisabled) expect(wrapper.emitted('on-redo')).toBeFalsy()
      else expect(wrapper.emitted('on-redo')).toBeTruthy()
    }
  )

  it.each`
    key                 | expectedEvent
    ${CTRL_SHIFT_ENTER} | ${'on-apply-script'}
    ${META_SHIFT_ENTER} | ${'on-apply-script'}
  `('should emit $expectedEvent when key $key is pressed', ({ key, expectedEvent }) => {
    wrapper.vm.shortKeyHandler(key)
    expect(wrapper.emitted(expectedEvent)[0]).toBeTruthy()
  })

  it('should pass shortKeyHandler to workspace.useShortKeyListener', () => {
    const spy = vi.spyOn(workspace, 'useShortKeyListener')
    wrapper = mountFactory()
    expect(spy).toHaveBeenCalledWith({ handler: wrapper.vm.shortKeyHandler, injectKeys: [WS_KEY] })
  })

  it.each`
    btn
    ${'link-shape-select'}
    ${'attr-to-attr-btn'}
    ${'auto-arrange-btn'}
    ${'relationship-highlight-btn'}
    ${'undo-btn'}
    ${'redo-btn'}
    ${'create-tbl-btn'}
    ${'export-btn'}
    ${'apply-btn'}
    ${'gen-erd-btn'}
  `(`Should render $btn`, async ({ btn }) => {
    expect(find(wrapper, btn).exists()).toBe(true)
  })

  it('Should pass expected data to link-shape-select', () => {
    const {
      $props: { modelValue, items, maxWidth, density, hideDetails },
      class: className,
    } = find(wrapper, 'link-shape-select').vm
    expect(modelValue).toBe(wrapper.vm.graphConfig.linkShape.type)
    expect(items).toStrictEqual(wrapper.vm.ALL_LINK_SHAPES)
    expect(className).toContain('borderless-input')
    expect(maxWidth).toBe(64)
    expect(density).toBe('compact')
    expect(hideDetails).toBe(true)
  })

  it('Should emit change-graph-config-attr-value event when link shape is changed', async () => {
    const newShape = LINK_SHAPES.STRAIGHT
    await find(wrapper, 'link-shape-select').vm.$emit('update:modelValue', newShape)
    expect(wrapper.emitted('change-graph-config-attr-value')[0]).toEqual([
      { path: 'linkShape.type', value: newShape },
    ])
  })

  it.each`
    btn                             | path
    ${'attr-to-attr-btn'}           | ${'link.isAttrToAttr'}
    ${'relationship-highlight-btn'} | ${'link.isHighlightAll'}
  `(`Clicking on $btn should emit expected event to update graph config`, async ({ btn, path }) => {
    const currVal = lodash.get(wrapper.vm.graphConfig, path)
    await find(wrapper, btn).trigger('click')
    expect(wrapper.emitted('change-graph-config-attr-value')[0]).toEqual([
      { path, value: !currVal },
    ])
  })

  it('Should emit set-zoom event when zoomRatio changes', async () => {
    wrapper.vm.zoomRatio = propsStub.zoom + 1
    await wrapper.vm.$nextTick()
    expect(wrapper.emitted('set-zoom')[0]).toEqual([{ v: propsStub.zoom + 1 }])
  })

  it('Should emit set-zoom event with isFitIntoView property when ZoomController updates isFitIntoView ', async () => {
    const isFitIntoView = !propsStub.isFitIntoView
    await wrapper
      .findComponent({ name: 'ZoomController' })
      .vm.$emit('update:isFitIntoView', isFitIntoView)

    expect(wrapper.emitted('set-zoom')[0]).toEqual([{ isFitIntoView }])
  })

  it.each`
    activeHistoryIdx | expected
    ${0}             | ${true}
    ${1}             | ${false}
  `(
    'Disabled props of undo-btn btn should be $expected when activeHistoryIdx is $activeHistoryIdx',
    async ({ activeHistoryIdx, expected }) => {
      await wrapper.setProps({ activeHistoryIdx })
      expect(find(wrapper, 'undo-btn').vm.$props.disabled).toBe(expected)
    }
  )

  it.each`
    props                      | expected
    ${disableRedoBtnMockProps} | ${true}
    ${enableRedoBtnMockProps}  | ${false}
  `('Disabled props of redo-btn btn should be $expected', async ({ props, expected }) => {
    await wrapper.setProps(props)
    expect(find(wrapper, 'redo-btn').vm.$props.disabled).toBe(expected)
  })

  it.each`
    btn                   | event
    ${'auto-arrange-btn'} | ${'click-auto-arrange'}
    ${'undo-btn'}         | ${'on-undo'}
    ${'redo-btn'}         | ${'on-redo'}
    ${'create-tbl-btn'}   | ${'on-create-table'}
    ${'apply-btn'}        | ${'on-apply-script'}
  `(`Should emit $event event when $btn is clicked`, async ({ btn, event }) => {
    // mock button clickable
    if (btn === 'undo-btn') await wrapper.setProps({ activeHistoryIdx: 1 })
    else if (btn === 'redo-btn') await wrapper.setProps({ activeHistoryIdx: 0 })

    await find(wrapper, btn).trigger('click')
    expect(wrapper.emitted(event)[0]).toBeTruthy()
  })

  it.each`
    btn              | fn
    ${'gen-erd-btn'} | ${'genErd'}
    ${'conn-btn'}    | ${'openCnnDlg'}
  `(`Should call $fn function when $btn is clicked`, async ({ btn, fn }) => {
    const spy = vi.spyOn(wrapper.vm, fn)
    await find(wrapper, btn).trigger('click')
    expect(spy).toHaveBeenCalled()
  })
})
