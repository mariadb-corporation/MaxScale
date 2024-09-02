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
import InsightViewer from '@wkeComps/QueryEditor/InsightViewer.vue'
import InsightViewerModel from '@wsModels/InsightViewer'
import queryResultService from '@wsServices/queryResultService'
import { lodash } from '@/utils/helpers'
import { NODE_TYPE_MAP, INSIGHT_SPEC_MAP } from '@/constants/workspace'

const { SCHEMA, TBL, VIEW, TRIGGER, SP, FN } = NODE_TYPE_MAP

const mountFactory = (opts = {}) =>
  mount(
    InsightViewer,
    lodash.merge(
      {
        shallow: false,
        props: { dim: { width: 800, height: 600 }, queryTab: { id: 'test-query-tab-id' } },
      },
      opts
    )
  )

describe(`InsightViewer`, () => {
  let wrapper

  vi.mock('@wsModels/InsightViewer', async (importOriginal) => {
    const Original = await importOriginal()
    return {
      default: class extends Original.default {
        static find = vi.fn()
      },
    }
  })

  vi.mock('@wsServices/queryResultService', async (importOriginal) => ({
    default: {
      ...(await importOriginal),
      queryInsightData: vi.fn(),
    },
  }))

  afterEach(() => vi.clearAllMocks())

  it.each`
    type
    ${SCHEMA}
    ${TBL}
    ${VIEW}
    ${TRIGGER}
    ${SP}
    ${FN}
  `('Should return expected specs correctly if active_node type is $type', ({ type }) => {
    InsightViewerModel.find.mockReturnValue({ active_spec: '', active_node: { type } })
    wrapper = mountFactory()
    let expectedSpecMap = []
    switch (type) {
      case SCHEMA:
        expectedSpecMap = Object.values(INSIGHT_SPEC_MAP).filter(
          (s) => s !== INSIGHT_SPEC_MAP.CREATION_INFO
        )
        break
      case TBL:
        expectedSpecMap = [
          INSIGHT_SPEC_MAP.COLUMNS,
          INSIGHT_SPEC_MAP.INDEXES,
          INSIGHT_SPEC_MAP.TRIGGERS,
          INSIGHT_SPEC_MAP.DDL,
        ]
        break
      case VIEW:
      case TRIGGER:
      case SP:
      case FN:
        expectedSpecMap = [INSIGHT_SPEC_MAP.CREATION_INFO, INSIGHT_SPEC_MAP.DDL]
    }
    expect(expectedSpecMap).toStrictEqual(wrapper.vm.specs)
  })

  it('Should queryInsightData service when query function is called', async () => {
    wrapper = mountFactory()
    const arg = {
      statement: { text: 'SELECT * FROM t1 LIMIT 100', limit: 100, offset: 0, type: 'select' },
      spec: INSIGHT_SPEC_MAP.CREATION_INFO,
    }
    await wrapper.vm.query(arg)
    expect(queryResultService.queryInsightData).toHaveBeenCalledWith(arg)
  })
})
