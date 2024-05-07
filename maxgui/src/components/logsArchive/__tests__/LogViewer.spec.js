/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import LogViewer from '@/components/logsArchive/LogViewer.vue'
import { lodash } from '@/utils/helpers'
import { stubLogData } from '@/components/logsArchive/__tests__/stubs'

const store = createStore({
  state: {
    logs: {
      prev_log_link: null, // // prevent fetchAndPrependPrevLogs from being called recursively
      prev_logs: [],
      latest_logs: [],
      log_filter: {},
    },
  },
  getters: {
    'logs/logDateRangeTimestamp': () => [],
  },
})

const mountFactory = (opts) =>
  mount(LogViewer, lodash.merge({ props: { height: 500 } }, opts), store)

const mockRes = { data: { data: stubLogData, links: { prev: null } } }

const fetchLatestMock = vi.hoisted(() => vi.fn(() => mockRes))

describe('LogViewer', () => {
  let wrapper

  vi.mock('@/services/logsService', async (importOriginal) => ({
    default: {
      ...(await importOriginal),
      fetchLatest: fetchLatestMock,
      fetchPrev: vi.fn(() => mockRes),
    },
  }))

  afterEach(() => vi.clearAllMocks())

  it('Should pass expected data to DynamicScroller', () => {
    wrapper = mountFactory()
    const { items, minItemSize } = wrapper.findComponent({ name: 'DynamicScroller' }).vm.$props
    expect(items).toStrictEqual(wrapper.vm.logs)
    expect(minItemSize).toBe(24)
  })

  it(`Should show no logs found when logs is empty`, async () => {
    const DynamicScrollerStub = {
      name: 'DynamicScrollerStub',
      template: `
      <div>
        <slot name="before"></slot>
        <slot :item="{}" :index="0" :active="true"></slot>
      </div>`,
    }
    wrapper = mountFactory({
      shallow: false,
      global: { stubs: { DynamicScroller: DynamicScrollerStub, DynamicScrollerItem: true } },
    })
    await wrapper.vm.$nextTick(() => expect(find(wrapper, 'no-logs').exists()).toBe(true))
  })

  it(`Should call fetchLatest onBeforeMount`, async () => {
    wrapper = mountFactory()
    await wrapper.vm.$nextTick()
    expect(fetchLatestMock).toHaveBeenCalled()
  })
})
