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
 * Public License.
 */
import mount from '@/tests/mount'
import WorkspaceCtr from '@wsComps/WorkspaceCtr.vue'
import { createStore } from 'vuex'
import { lodash } from '@/utils/helpers'

const activeWke = {
  id: 'e5e2f3e0-fe32-11ee-828f-393814e2a3b0',
  name: 'WORKSHEET',
  erd_task_id: null,
  etl_task_id: null,
  query_editor_id: null,
}
const mockStore = createStore({
  state: {
    prefAndStorage: { is_fullscreen: false },
    workspace: { hidden_comp: [] },
  },
  getters: {
    'worksheets/activeRecord': () => activeWke,
    'worksheets/activeId': () => activeWke.id,
  },
  commit: vi.fn(),
})

const mountFactory = (opts) =>
  mount(
    WorkspaceCtr,
    lodash.merge({ global: { directives: { shortkey: true }, stubs: { SqlEditor: true } } }, opts),
    mockStore
  )

describe('WorkspaceCtr', () => {
  let wrapper
  const autoClearQueryHistoryMock = vi.hoisted(() => vi.fn())

  vi.mock('@wsServices/prefAndStorageService', async (importOriginal) => ({
    default: {
      ...(await importOriginal),
      autoClearQueryHistory: autoClearQueryHistoryMock,
    },
  }))

  afterEach(() => vi.clearAllMocks())

  it(`Should call autoClearQueryHistory when component is created`, () => {
    mountFactory()
    expect(autoClearQueryHistoryMock).toHaveBeenCalled()
  })

  it('Should render BlankWke component when the active worksheet is blank', async () => {
    wrapper = mountFactory()
    wrapper.vm.dim = { width: 1000, height: 500 }
    await wrapper.vm.$nextTick()
    expect(wrapper.findComponent({ name: 'BlankWke' }).exists()).toBe(true)
  })
})
