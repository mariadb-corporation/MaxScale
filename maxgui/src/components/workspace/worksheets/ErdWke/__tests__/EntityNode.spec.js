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
import EntityNode from '@wkeComps/ErdWke/EntityNode.vue'
import { lodash } from '@/utils/helpers'
import { CREATE_TBL_TOKEN_MAP } from '@/constants/workspace'
import erdHelper from '@/utils/erdHelper'

const { primaryKey, uniqueKey, key, fullTextKey, spatialKey, foreignKey } = CREATE_TBL_TOKEN_MAP

const colMapStub = Array.from({ length: 6 }).reduce((map, _, i) => {
  map[`col_${i}`] = { name: `Column${i}`, data_type: 'INT' }
  return map
}, {})

const nodeStub = {
  data: { options: { name: 'test-table' }, defs: { col_map: colMapStub } },
  styles: { highlightColor: '#0e9bc0' },
}

const mountFactory = (opts = {}, store) =>
  mount(
    EntityNode,
    lodash.merge(
      {
        shallow: false,
        props: {
          node: nodeStub,
          headerHeight: '32px',
          rowHeight: '32px',
          colKeyCategoryMap: {},
          keyCategoryMap: {},
          highlightColStyleMap: {},
          isDrawingFk: true,
        },
        global: { stubs: { GblTooltipActivator: true, ErdKeyIcon: true } },
      },
      opts
    ),
    store
  )

describe(`EntityNode`, () => {
  let wrapper

  vi.mock('@/utils/erdHelper', async (importOriginal) => ({
    default: { ...(await importOriginal()).default, isSingleUQ: vi.fn(() => false) },
  }))

  afterEach(() => {
    vi.clearAllMocks()
  })

  it('Should render entity name correctly', () => {
    wrapper = mountFactory()
    expect(find(wrapper, 'entity-name').text()).toBe(nodeStub.data.options.name)
  })

  it('Should compute nodeData', () => {
    wrapper = mountFactory()
    expect(wrapper.vm.nodeData).toStrictEqual(nodeStub.data)
  })

  it('Should compute highlightColor', () => {
    wrapper = mountFactory()
    expect(wrapper.vm.highlightColor).toBe(nodeStub.styles.highlightColor)
  })

  it.each`
    colId      | isUQ     | categories                  | expectedIcon            | expectedColor
    ${'col_0'} | ${false} | ${[primaryKey, foreignKey]} | ${'$mdiKey'}            | ${'blue'}
    ${'col_1'} | ${true}  | ${[uniqueKey]}              | ${'mxs:uniqueIndexKey'} | ${'green'}
    ${'col_2'} | ${false} | ${[key]}                    | ${'mxs:indexKey'}       | ${'yellow'}
    ${'col_3'} | ${false} | ${[fullTextKey]}            | ${'mxs:indexKey'}       | ${'red'}
    ${'col_4'} | ${false} | ${[spatialKey]}             | ${'mxs:indexKey'}       | ${'orange'}
    ${'col_5'} | ${false} | ${[foreignKey]}             | ${'mxs:indexKey'}       | ${'white'}
    ${'col_6'} | ${false} | ${[]}                       | ${undefined}            | ${undefined}
  `(
    'Should return correct icon and color when the col is $categories',
    ({ colId, isUQ, categories, expectedIcon, expectedColor }) => {
      erdHelper.isSingleUQ.mockReturnValue(isUQ)

      wrapper = mountFactory({
        props: {
          colKeyCategoryMap: { [colId]: categories },
          highlightColStyleMap: { [colId]: { color: expectedColor } },
        },
      })

      const keyIcon = wrapper.vm.getKeyIcon(colId)
      if (categories.length) {
        expect(keyIcon.icon).toBe(expectedIcon)
        expect(keyIcon.color).toBe(expectedColor)
      } else expect(keyIcon).toBe(undefined)
    }
  )
})
