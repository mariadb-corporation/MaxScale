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
import TxtEditorToolbarCtr from '@wkeComps/QueryEditor/TxtEditorToolbarCtr.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    TxtEditorToolbarCtr,
    lodash.merge(
      {
        shallow: false,
        props: {
          height: 30,
          queryTab: { id: 'QUERY_TAB_123_45' },
          queryTabTmp: {},
          queryTabConn: {},
          queryTxt: '',
          selectedQueryTxt: '',
          isVisSidebarShown: false,
        },
        global: {
          stubs: { SqlEditor: true },
          provide: { WS_EMITTER_KEY: 'WS_EMITTER_KEY', EDITOR_EMITTER_KEY: 'EDITOR_EMITTER_KEY' },
        },
      },
      opts
    )
  )
describe(`TxtEditorToolbarCtr`, () => {
  let wrapper

  afterEach(() => vi.clearAllMocks())

  it(`Should pass accurate data to BaseDlg`, () => {
    wrapper = mountFactory()
    const { modelValue, title, onSave, closeImmediate, saveText } = wrapper.findComponent({
      name: 'BaseDlg',
    }).vm.$props
    expect(modelValue).toBe(wrapper.vm.confDlg.isOpened)
    expect(title).toBe(wrapper.vm.confDlg.title)
    expect(saveText).toBe(wrapper.vm.confDlg.type)
    expect(onSave).toStrictEqual(wrapper.vm.confDlg.onSave)
    expect(closeImmediate).toBe(true)
  })

  it(`Should render RowLimit`, () => {
    wrapper = mountFactory()
    const {
      $props: { modelValue, minimized, showErrInSnackbar },
      $attrs: { prefix, 'hide-details': hideDetails },
    } = wrapper.findComponent({ name: 'RowLimit' }).vm
    expect(modelValue).toBe(wrapper.vm.rowLimit)
    expect(minimized).toBe(true)
    expect(showErrInSnackbar).toBe(true)
    expect(prefix).toBe(wrapper.vm.$t('defLimit'))
    expect(hideDetails).toBeDefined()
  })

  const queryTxtTestCases = ['', 'SELECT 1']
  queryTxtTestCases.forEach((queryTxt) => {
    it(`Should ${queryTxt ? 'not disable' : 'disable'} save to snippets button`, () => {
      wrapper = mountFactory({ props: { queryTxt } })
      expect(find(wrapper, 'create-snippet-btn').element.disabled).toBe(!queryTxt)
    })
  })

  it(`Should popup dialog to save query text to snippets`, () => {
    expect(wrapper.vm.confDlg.isOpened).toBe(false)
    wrapper = mountFactory({ props: { queryTxt: 'SELECT 1' } })
    find(wrapper, 'create-snippet-btn').trigger('click')
    expect(wrapper.vm.confDlg.isOpened).toBe(true)
  })

  it(`Should generate snippet object before popup the dialog`, () => {
    wrapper = mountFactory({ props: { queryTxt: 'SELECT 1' } })
    find(wrapper, 'create-snippet-btn').trigger('click')
    expect(wrapper.vm.snippet.name).toBe('')
    expect(wrapper.vm.snippet.date).toBeTypeOf('number')
  })

  it(`Should assign addSnippet as the save handler for confDlg`, () => {
    wrapper = mountFactory({ props: { queryTxt: 'SELECT 1' } })
    wrapper.vm.openSnippetDlg()
    expect(wrapper.vm.confDlg.onSave).toStrictEqual(wrapper.vm.addSnippet)
  })

  const btns = ['run-btn', 'visualize-btn']
  btns.forEach((btn) => {
    it(
      btn === 'run-btn'
        ? `Should render 'stop-btn' if there is a running query`
        : `Should disable ${btn} if there is a running query`,
      () => {
        // mock isExecuting computed property
        wrapper = mountFactory({ props: { queryTabTmp: { query_results: { is_loading: true } } } })
        if (btn === 'run-btn') expect(find(wrapper, 'stop-btn').exists()).toBe(true)
        else expect(find(wrapper, btn).element.disabled).toBe(true)
      }
    )

    it(`${btn} should be clickable if it matches certain conditions`, () => {
      wrapper = mountFactory({
        props: {
          queryTxt: 'SELECT 1',
          queryTabConn: { id: 'id', is_busy: false },
          queryTabTmp: { query_results: { is_loading: false } },
        },
      })
      expect(find(wrapper, btn).element.disabled).toBe(false)
    })
  })

  it(`Should popup query confirmation dialog with accurate data`, () => {
    wrapper = mountFactory({
      props: {
        queryTxt: 'SELECT 1',
        queryTabConn: { id: 'id', is_busy: false },
        queryTabTmp: { query_results: { is_loading: false } },
      },
    })
    expect(wrapper.vm.query_confirm_flag).toBe(true)
    wrapper.vm.handleRun('all')
    expect(wrapper.vm.dontShowConfirm).toBe(false)
    expect(wrapper.vm.confDlg.isOpened).toBe(true)
  })
})
