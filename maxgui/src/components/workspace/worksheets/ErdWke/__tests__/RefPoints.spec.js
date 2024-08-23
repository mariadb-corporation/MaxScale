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
import RefPoints from '@wkeComps/ErdWke/RefPoints.vue'

const mockNode = {
  x: 100,
  y: 100,
  size: { width: 200, height: 200 },
  data: { defs: { col_map: { col1: { id: 'col1' }, col2: { id: 'col2' } } } },
  styles: { highlightColor: 'blue' },
}
const mockEntitySizeConfig = { headerHeight: 20, rowHeight: 30 }
const mockLinkContainer = {
  append: vi.fn(() => ({ attr: vi.fn().mockReturnThis(), remove: vi.fn() })),
}
const mockGraphConfig = { linkShape: { type: 'line' }, marker: { width: 10 } }

describe('RefPoints', () => {
  let wrapper
  beforeEach(
    () =>
      (wrapper = mount(RefPoints, {
        shallow: false,
        props: {
          node: mockNode,
          entitySizeConfig: mockEntitySizeConfig,
          linkContainer: mockLinkContainer,
          boardZoom: 1,
          graphConfig: mockGraphConfig,
        },
      }))
  )
  it('Should renders reference points as expected', () => {
    const points = wrapper.findAll(`[data-test="ref-point"]`)
    expect(points.length).toBe(2 * Object.keys(mockNode.data.defs.col_map).length)
  })

  it('Should call dragStart function on mousedown', async () => {
    const spy = vi.spyOn(wrapper.vm, 'dragStart')
    const point = find(wrapper, 'ref-point')
    await point.trigger('mousedown')
    expect(spy).toHaveBeenCalled(1)
  })

  it('Should add mousemove and mouseup events when dragStart is called', () => {
    const spy = vi.spyOn(document, 'addEventListener')
    wrapper.vm.dragStart({
      e: { clientX: 100, clientY: 100 },
      point: {
        col: { id: 'col1' },
        pos: { x: 0, y: 10 },
        pointDirection: 'LEFT',
      },
    })
    expect(spy).toHaveBeenCalledWith('mousemove', wrapper.vm.drawing)
    expect(spy).toHaveBeenCalledWith('mouseup', wrapper.vm.drawEnd)
  })

  it('Should emit drawing event', async () => {
    const point = find(wrapper, 'ref-point')
    await point.trigger('mousedown')
    const mousemoveEvent = new MouseEvent('mousemove', { clientX: 150, clientY: 150 })
    document.dispatchEvent(mousemoveEvent)
    expect(wrapper.emitted().drawing).toBeTruthy()
  })

  it('Should emit draw-end event on mouseup', async () => {
    const point = find(wrapper, 'ref-point')
    await point.trigger('mousedown')
    const mouseupEvent = new MouseEvent('mouseup')
    document.dispatchEvent(mouseupEvent)
    expect(wrapper.emitted()['draw-end']).toBeTruthy()
  })
})
