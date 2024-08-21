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
import CreateNode from '@wkeComps/QueryEditor/CreateNode.vue'
import { lodash } from '@/utils/helpers'
import { NODE_TYPE_MAP } from '@/constants/workspace'
import { SNACKBAR_TYPE_MAP } from '@/constants'

const mountFactory = (opts) =>
  mount(
    CreateNode,
    lodash.merge(
      {
        shallow: false,
        props: {
          activeDb: '',
          disabled: false,
        },
      },
      opts
    )
  )

describe(`CreateNode`, () => {
  let wrapper

  beforeEach(() => (wrapper = mountFactory()))
  it(`Should pass expected data to CtxMenu`, () => {
    const {
      $props: { items },
      $attrs: { disabled },
    } = wrapper.findComponent({
      name: 'CtxMenu',
    }).vm
    expect(items).toBe(wrapper.vm.CREATE_OPTIONS)
    expect(disabled).toBe(wrapper.vm.$props.disabled)
  })

  it('Should disabled the menu and button when disabled prop is true', async () => {
    await wrapper.setProps({ disabled: true })
    expect(wrapper.findComponent({ name: 'TooltipBtn' }).vm.$attrs.disabled).toBe(true)
    expect(wrapper.findComponent({ name: 'CtxMenu' }).vm.$attrs.disabled).toBe(true)
  })

  it('Should emit create-node event with correct payload when a schema node is selected', async () => {
    const ctxMenu = wrapper.findComponent({ name: 'CtxMenu' })
    await ctxMenu.vm.$emit('item-click', { targetNodeType: NODE_TYPE_MAP.SCHEMA })
    expect(wrapper.emitted('create-node')).toHaveLength(1)
    expect(wrapper.emitted('create-node')[0]).toEqual([{ type: NODE_TYPE_MAP.SCHEMA }])
  })

  it('emits create-node event with correct payload when a non-schema node is selected', async () => {
    const stubActiveDb = '`test`'
    await wrapper.setProps({ activeDb: stubActiveDb })
    const ctxMenu = wrapper.findComponent({ name: 'CtxMenu' })
    await ctxMenu.vm.$emit('item-click', {
      targetNodeType: NODE_TYPE_MAP.TBL,
    })
    expect(wrapper.emitted('create-node')).toHaveLength(1)
    expect(wrapper.emitted('create-node')[0]).toEqual([
      {
        type: NODE_TYPE_MAP.TBL,
        parentNameData: { [NODE_TYPE_MAP.SCHEMA]: 'test' },
      },
    ])
  })

  it('Should show error when non-schema node is selected without activeDb', async () => {
    const ctxMenu = wrapper.findComponent({ name: 'CtxMenu' })
    const storeCommitSpy = vi.spyOn(wrapper.vm.$store, 'commit')
    await ctxMenu.vm.$emit('item-click', { targetNodeType: NODE_TYPE_MAP.TBL })
    expect(storeCommitSpy).toHaveBeenCalledWith('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [wrapper.vm.$t('errors.requiredActiveSchema')],
      type: SNACKBAR_TYPE_MAP.ERROR,
    })
  })
})
