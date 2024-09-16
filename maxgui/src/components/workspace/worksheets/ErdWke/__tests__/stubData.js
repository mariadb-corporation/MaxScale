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
import { editorDataStub } from '@wsComps/TblStructureEditor/__tests__/stubData'
import { LINK_SHAPES } from '@/components/svgGraph/shapeConfig'

export const erdTaskStub = {
  id: '65c02181-740a-11ef-b24c-21c58399ad86',
  nodeMap: {
    [editorDataStub.id]: {
      id: editorDataStub.id,
      data: editorDataStub,
      styles: { highlightColor: 'rgba(171,199,74,1)' },
      x: 0,
      y: 0,
      vx: 0,
      vy: 0,
    },
  },
  count: 1,
  graph_config: {
    link: { isAttrToAttr: false, isHighlightAll: false },
    linkShape: { type: LINK_SHAPES.ORTHO },
  },
  is_laid_out: false,
  connection: null,
  erdTaskTmp: null,
}

export const erdTaskTempStub = {
  id: erdTaskStub.id,
  graph_height_pct: 100,
  active_entity_id: editorDataStub.id,
  active_spec: 'columns',
  key: '7b957aa0-740f-11ef-a06b-95dcc73aac2c',
  nodes_history: [],
  active_history_idx: 0,
}
