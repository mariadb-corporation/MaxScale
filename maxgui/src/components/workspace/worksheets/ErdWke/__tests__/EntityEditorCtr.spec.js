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
import EntityEditorCtr from '@wkeComps/ErdWke/EntityEditorCtr.vue'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import schemaInfoService from '@wsServices/schemaInfoService'
import { lodash } from '@/utils/helpers'
import { editorDataStub } from '@wsComps/TblStructureEditor/__tests__/stubData'
import { SNACKBAR_TYPE_MAP } from '@/constants'

const propsStub = {
  dim: {},
  node: {},
  taskId: 'task-1',
  connId: 'conn-1',
  tables: [],
  schemas: [],
  erdTaskTmp: {},
}

const mountFactory = (opts = {}, store) =>
  mount(EntityEditorCtr, lodash.merge({ props: propsStub }, opts), store)

describe(`EntityEditorCtr`, () => {
  let wrapper

  vi.mock('@wsModels/ErdTaskTmp', async (importOriginal) => {
    const OriginalEditor = await importOriginal()
    return {
      default: class extends OriginalEditor.default {
        static update = vi.fn()
      },
    }
  })

  vi.mock('@wsServices/schemaInfoService', async (importOriginal) => ({
    default: { ...(await importOriginal), querySuppData: vi.fn() },
  }))

  afterEach(() => {
    vi.clearAllMocks()
  })

  it.each`
    case            | expectedRender | when
    ${'not render'} | ${false}       | ${'when stagingData is an empty object'}
    ${'render'}     | ${true}        | ${'when stagingData is not an empty object'}
  `(`Should $case TblStructureEditor $when`, ({ expectedRender }) => {
    wrapper = mountFactory({ props: { data: expectedRender ? editorDataStub : {} } })
    expect(wrapper.findComponent({ name: 'TblStructureEditor' }).exists()).toBe(expectedRender)
  })

  it('Should pass expected data to TblStructureEditor', () => {
    wrapper = mountFactory({ props: { data: editorDataStub } })
    const {
      modelValue,
      activeSpec,
      dim,
      initialData,
      isCreating,
      schemas,
      lookupTables,
      connReqData,
      showApplyBtn,
    } = wrapper.findComponent({ name: 'TblStructureEditor' }).vm.$props
    expect(modelValue).toStrictEqual(wrapper.vm.stagingData)
    expect(activeSpec).toBe(wrapper.vm.activeSpec)
    expect(dim).toStrictEqual(wrapper.vm.$props.dim)
    expect(initialData).toStrictEqual({})
    expect(isCreating).toBe(true)
    expect(schemas).toStrictEqual(wrapper.vm.$props.schemas)
    expect(lookupTables).toStrictEqual(wrapper.vm.lookupTables)
    expect(connReqData).toStrictEqual(wrapper.vm.connReqData)
    expect(showApplyBtn).toBe(false)
  })

  it('Should query supplement data when the component is mounted', () => {
    const spy = vi.spyOn(schemaInfoService, 'querySuppData')
    wrapper = mountFactory()
    const connReqData = wrapper.vm.connReqData
    expect(spy).toHaveBeenNthCalledWith(1, { connId: connReqData.id, config: connReqData.config })
  })

  it('Should append close-btn in TblStructureEditor toolbar', () => {
    const wrapper = mountFactory({ shallow: false, props: { data: editorDataStub } })
    expect(find(wrapper, 'close-btn').exists()).toBe(true)
  })

  it('Should call close function when close-btn is clicked', async () => {
    wrapper = mountFactory({ shallow: false, props: { data: editorDataStub } })
    const spy = vi.spyOn(wrapper.vm, 'close')
    await find(wrapper, 'close-btn').trigger('click')
    expect(spy).toHaveBeenCalled()
  })

  it.each`
    case                              | validateRes | condition
    ${'display a validation message'} | ${false}    | ${'left empty'}
    ${'update ErdTaskTmp model'}      | ${true}     | ${'not left empty'}
  `(
    `Clicking on close-btn should $case when required fields in the editor are $condition `,
    async ({ validateRes }) => {
      const mockStore = createStore({
        mutations: { 'mxsApp/SET_SNACK_BAR_MESSAGE': vi.fn() },
      })
      mockStore.commit = vi.fn()
      wrapper = mountFactory({}, mockStore)

      wrapper.vm.editorRef = { validate: vi.fn().mockResolvedValue(validateRes) }
      await wrapper.vm.$nextTick()

      await wrapper.vm.close()
      if (validateRes)
        expect(ErdTaskTmp.update).toHaveBeenCalledWith({
          where: propsStub.taskId,
          data: { graph_height_pct: 100, active_entity_id: '' },
        })
      else
        expect(mockStore.commit).toHaveBeenCalledWith('mxsApp/SET_SNACK_BAR_MESSAGE', {
          text: [wrapper.vm.$t('errors.requiredFields')],
          type: SNACKBAR_TYPE_MAP.ERROR,
        })
    }
  )

  it('Should emit change event when stagingData changes', async () => {
    wrapper = mountFactory({ shallow: false, props: { data: editorDataStub } })
    wrapper.vm.stagingData.options.name = 'new name'
    await wrapper.vm.$nextTick()
    expect(wrapper.emitted('change')[0][0]).toStrictEqual(wrapper.vm.stagingData)
  })
})
