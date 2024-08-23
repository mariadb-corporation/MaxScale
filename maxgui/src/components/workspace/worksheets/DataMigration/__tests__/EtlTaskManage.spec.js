/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import EtlTaskManage from '@wkeComps/DataMigration/EtlTaskManage.vue'
import { ETL_ACTION_MAP } from '@/constants/workspace'
import { lodash } from '@/utils/helpers'

const typesStub = Object.values(ETL_ACTION_MAP)

const etlTaskObjStub = {
  id: 'c74d6e00-4263-11ee-a879-6f8dfc9ca55f',
  status: 'Initializing',
}

const mountFactory = (opts) =>
  mount(EtlTaskManage, lodash.merge({ props: { task: etlTaskObjStub, types: typesStub } }, opts))

const actionHandlerMock = vi.hoisted(() => vi.fn())

vi.mock('@wsServices/etlTaskService', async (importOriginal) => ({
  default: {
    ...(await importOriginal),
    actionHandler: actionHandlerMock,
  },
}))

describe('EtlTaskManage', () => {
  let wrapper

  afterEach(() => vi.clearAllMocks())

  it('Should emit "on-restart" event when RESTART action is chosen', async () => {
    wrapper = mountFactory()
    await wrapper.vm.handler(ETL_ACTION_MAP.RESTART)
    expect(wrapper.emitted('on-restart')[0][0]).toBe(etlTaskObjStub.id)
  })

  it(`Should render activator slot`, () => {
    wrapper = mountFactory({
      shallow: false,
      slots: { ['activator']: '<div data-test="activator"/>' },
    })
    expect(find(wrapper, 'activator').exists()).toBe(true)
  })

  Object.values(ETL_ACTION_MAP).forEach((action) => {
    if (action !== ETL_ACTION_MAP.RESTART)
      it(`Should dispatch EtlTask actionHandler when ${action} is chosen`, async () => {
        wrapper = mountFactory()
        await wrapper.vm.handler(action)
        expect(actionHandlerMock).toHaveBeenCalledWith({
          type: action,
          task: wrapper.vm.$props.task,
        })
      })
  })
})
