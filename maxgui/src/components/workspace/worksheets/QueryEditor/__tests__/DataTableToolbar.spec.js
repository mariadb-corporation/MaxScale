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
import DataTableToolbar from '@wkeComps/QueryEditor/DataTableToolbar.vue'
import { lodash } from '@/utils/helpers'
import { NO_LIMIT } from '@/constants/workspace'
import { enforceLimitOffset, enforceNoLimit } from '@/utils/sqlLimiter'

const modelProps = {
  search: '',
  excludedSearchHeaderIndexes: [],
  activeGroupByColIdx: -1,
  hiddenHeaderIndexes: [],
  isVertTable: false,
}

const mountFactory = (opts) => mount(DataTableToolbar, lodash.merge({ shallow: false }, opts))
const selectStmtStub = { type: 'select', text: 'SELECT * from t1 LIMIT 10', limit: 10 }
const showStmtStub = { type: 'show', text: 'SHOW PROCESSLIST', limit: 100 }

const assertErrorAndStatement = ({ result, expectedErrMsg, expectedStatement }) => {
  const [errMsg, statement] = result
  expect(errMsg).toBe(expectedErrMsg)
  expect(statement).toStrictEqual(expectedStatement)
}

async function mockNoLimitOpt(wrapper) {
  wrapper.vm.rowLimit = NO_LIMIT
  await wrapper.vm.$nextTick()
}

describe(`DataTableToolbar`, () => {
  let wrapper

  vi.mock('@/utils/sqlLimiter', () => ({
    getStatementClasses: vi.fn(() => [null, null]),
    enforceNoLimit: vi.fn(() => [null, null]),
    enforceLimitOffset: vi.fn(() => [null, null]),
  }))

  beforeEach(() => {
    wrapper = mountFactory({
      props: {
        showBtn: true,
        ...modelProps,
        ...Object.keys(modelProps).reduce((acc, key) => {
          acc[`onUpdate:${key}`] = (v) => wrapper.setProps({ [`${key}`]: v })
          return acc
        }, {}),
      },
    })
  })

  afterEach(() => vi.clearAllMocks())

  it(`Should render RowLimit only when statement object is defined`, async () => {
    expect(wrapper.findComponent({ name: 'RowLimit' }).exists()).toBe(false)
    await wrapper.setProps({ statement: showStmtStub })
    expect(wrapper.findComponent({ name: 'RowLimit' }).exists()).toBe(true)
  })

  it(`Should pass expected data to RowLimit`, async () => {
    await wrapper.setProps({ statement: selectStmtStub })
    const {
      $props: { modelValue, minimized, borderless, showErrInSnackbar, hasNoLimit, allowEmpty },
      $attrs: { prefix },
    } = wrapper.findComponent({
      name: 'RowLimit',
    }).vm
    expect(modelValue).toBe(wrapper.vm.rowLimit)
    expect(prefix).toBe(wrapper.vm.$t('limit'))
    expect(minimized).toBe(true)
    expect(borderless).toBe(true)
    expect(showErrInSnackbar).toBe(true)
    expect(hasNoLimit).toBe(true)
    expect(allowEmpty).toBe(true)
  })

  it(`Should render OffsetInput only when select statement object is defined`, async () => {
    await wrapper.setProps({ statement: showStmtStub })
    expect(wrapper.findComponent({ name: 'OffsetInput' }).exists()).toBe(false)
    await wrapper.setProps({ statement: selectStmtStub })
    expect(wrapper.findComponent({ name: 'OffsetInput' }).exists()).toBe(true)
  })

  it(`Should not render OffsetInput when no limit option is chosen`, async () => {
    await wrapper.setProps({ statement: selectStmtStub })
    await mockNoLimitOpt(wrapper)
    expect(wrapper.findComponent({ name: 'OffsetInput' }).exists()).toBe(false)
  })

  it(`Should pass expected data to ResultExport`, () => {
    const { rows, fields, defExportFileName, exportAsSQL, metadata } = wrapper.findComponent({
      name: 'ResultExport',
    }).vm.$props
    expect(rows).toStrictEqual(wrapper.vm.$props.rows)
    expect(fields).toStrictEqual(wrapper.vm.$props.fields)
    expect(defExportFileName).toBe(wrapper.vm.$props.defExportFileName)
    expect(exportAsSQL).toBe(wrapper.vm.$props.exportAsSQL)
    expect(metadata).toStrictEqual(wrapper.vm.$props.metadata)
  })

  it(`Should render toolbar-left-append slot`, () => {
    wrapper = mountFactory({
      slots: { 'toolbar-left-append': '<div data-test="toolbar-left"/>' },
    })
    expect(find(wrapper, 'toolbar-left').exists()).toBe(true)
  })

  it(`Should render toolbar-right-prepend slot`, () => {
    wrapper = mountFactory({
      slots: { 'toolbar-right-prepend': '<div data-test="toolbar-right"/>' },
    })
    expect(find(wrapper, 'toolbar-right').exists()).toBe(true)
  })

  const buttons = [
    'reload-btn',
    'delete-btn',
    'filter-btn',
    'grouping-btn',
    'col-vis-btn',
    'rotate-btn',
  ]

  buttons.forEach((btn) => {
    // reload-btn && delete-btn aren't rendered by default even when showBtn props is true
    if (btn === 'delete-btn' || btn === 'reload-btn')
      it(`Should not render ${btn} even when showBtn props is true`, () => {
        expect(find(wrapper, btn).exists()).toBe(false)
      })
    else
      it(`Should render ${btn}`, () => {
        expect(find(wrapper, btn).exists()).toBe(true)
      })

    it(`Should not render ${btn} when showBtn props is false`, async () => {
      await wrapper.setProps({ showBtn: false })
      expect(find(wrapper, btn).exists()).toBe(false)
    })

    switch (btn) {
      case 'reload-btn':
        it(`Should only render ${btn} if onReload is defined`, async () => {
          await wrapper.setProps({ onReload: vi.fn() })
          expect(find(wrapper, btn).exists()).toBe(true)
        })
        it(`Should disabled ${btn} if form is not valid`, async () => {
          await wrapper.setProps({ onReload: vi.fn() })
          expect(find(wrapper, btn).vm.$props.disabled).toBe(!wrapper.vm.validity)
          wrapper.vm.validity = true
          await wrapper.vm.$nextTick()
          expect(find(wrapper, btn).vm.$props.disabled).toBe(false)
        })
        break
      case 'delete-btn':
        it(`Should only render ${btn} if onDelete is defined and selectedItems is not empty`, async () => {
          await wrapper.setProps({ selectedItems: ['a'], onDelete: vi.fn() })
          expect(find(wrapper, btn).exists()).toBe(true)
        })
        break
      case 'filter-btn':
        it(`Should render filter-input`, async () => {
          wrapper.vm.isFilterMenuOpened = true
          await wrapper.vm.$nextTick()
          expect(find(wrapper, 'filter-input').exists()).toBe(true)
        })
        it(`Should render filter-by dropdown`, async () => {
          wrapper.vm.isFilterMenuOpened = true
          await wrapper.vm.$nextTick()
          expect(find(wrapper, 'filter-by').exists()).toBe(true)
        })
        break
      case 'rotate-btn':
        it(`Should disable rotate-btn is activeGroupByColIdx >= 0`, async () => {
          expect(find(wrapper, btn).vm.$props.disabled).toBe(false)
          await wrapper.setProps({ activeGroupByColIdx: 0 })
          expect(find(wrapper, btn).vm.$props.disabled).toBe(true)
        })
        it(`Should toggle isVertTable value when rotate-btn is click`, async () => {
          expect(wrapper.vm.$props.isVertTable).toBe(false)
          await find(wrapper, btn).trigger('click')
          expect(wrapper.vm.$props.isVertTable).toBe(true)
        })
        break
    }
  })

  describe('setNoLimit', () => {
    it('Should return expected error message and null statement if enforcing no limit fails', () => {
      const mockStatementClass = {}
      enforceNoLimit.mockReturnValue([new Error('some error'), null])
      assertErrorAndStatement({
        result: wrapper.vm.setNoLimit(mockStatementClass),
        expectedErrMsg: wrapper.vm.$t('errors.enforceNoLimit'),
        expectedStatement: null,
      })
    })

    it('Should return null error and the updated statement if enforcing no limit succeeds', () => {
      const mockStatementClass = {}
      const mockStatement = { text: 'SELECT * FROM table', limit: 0, type: 'select' }
      enforceNoLimit.mockReturnValue([null, mockStatement])
      assertErrorAndStatement({
        result: wrapper.vm.setNoLimit(mockStatementClass),
        expectedErrMsg: null,
        expectedStatement: mockStatement,
      })
    })
  })

  describe('setLimitAndOffset', () => {
    it('Should return an error message and null statement if enforcing limit and offset fails', () => {
      const mockStatementClass = {}
      enforceLimitOffset.mockReturnValue([new Error('some error'), null])
      assertErrorAndStatement({
        result: wrapper.vm.setLimitAndOffset(mockStatementClass),
        expectedErrMsg: wrapper.vm.$t('errors.injectLimit'),
        expectedStatement: null,
      })
    })

    it('Should return null error and the updated statement if enforcing limit and offset succeeds', () => {
      const mockStatementClass = {}
      const mockStatement = {
        text: 'SELECT * FROM table limit 100 offset 5',
        limit: 100,
        offset: 5,
        type: 'select',
      }
      enforceLimitOffset.mockReturnValue([null, mockStatement])

      assertErrorAndStatement({
        result: wrapper.vm.setLimitAndOffset(mockStatementClass),
        expectedErrMsg: null,
        expectedStatement: mockStatement,
      })
    })
  })

  describe('handleApplyLimitAndOffset', () => {
    it('Should apply no limit if isNoLimit is true', async () => {
      const mockStatement = { text: 'SELECT * FROM table limit 100', limit: 100, type: 'select' }
      await wrapper.setProps({ statement: mockStatement })
      await mockNoLimitOpt(wrapper)

      const [errMsg, statement] = wrapper.vm.handleApplyLimitAndOffset(mockStatement)

      expect(errMsg).toBeNull()
      expect(statement).toStrictEqual({ text: 'SELECT * FROM table', limit: 0, type: 'select' })
    })

    it('Should enforce new limit and offset', async () => {
      const mockStatement = { text: 'SELECT * FROM table limit 100', limit: 100, type: 'select' }
      await wrapper.setProps({ statement: mockStatement })

      const expectedStatement = {
        text: 'SELECT * FROM table limit 300 offset 10',
        limit: 300,
        offset: 10,
        type: 'select',
      }
      enforceLimitOffset.mockReturnValue([null, expectedStatement])

      const [errMsg, statement] = wrapper.vm.handleApplyLimitAndOffset(mockStatement)
      expect(errMsg).toBeNull()
      expect(statement).toStrictEqual(expectedStatement)
    })

    it('should return an updated statement with new limit if not a select statement', async () => {
      const mockStatement = { text: 'SHOW PROCESSLIST', limit: 100, type: 'show' }
      await wrapper.setProps({ statement: mockStatement })
      wrapper.vm.rowLimit = 500
      await wrapper.vm.$nextTick()
      const [errMsg, statement] = wrapper.vm.handleApplyLimitAndOffset(mockStatement)

      expect(errMsg).toBeNull()
      expect(statement).toStrictEqual({ ...mockStatement, limit: 500 })
    })
  })

  describe('reload', () => {
    it('Should call onReload with updated statement if limit or offset has changed', async () => {
      const onReloadStub = vi.fn()
      await wrapper.setProps({
        onReload: onReloadStub,
        statement: { text: 'SELECT * FROM table LIMIT 10', limit: 10, type: 'select' },
      })
      const spy = vi.spyOn(wrapper.vm.$props, 'onReload')
      await mockNoLimitOpt(wrapper)

      const newStatement = { text: 'SELECT * FROM table', limit: 0, type: 'select' }
      wrapper.vm.handleApplyLimitAndOffset = vi.fn().mockReturnValue([null, newStatement])
      await wrapper.vm.$nextTick()

      await wrapper.vm.reload()
      expect(spy).toHaveBeenCalledWith(newStatement)
    })
  })
})
