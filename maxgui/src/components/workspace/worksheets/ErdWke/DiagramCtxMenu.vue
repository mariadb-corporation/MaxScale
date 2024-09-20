<script setup>
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
import { DIAGRAM_CTX_TYPE_MAP as TYPE } from '@/constants'
import {
  ENTITY_OPT_TYPE_MAP,
  LINK_OPT_TYPE_MAP,
  CREATE_TBL_TOKEN_MAP,
  TABLE_STRUCTURE_SPEC_MAP,
} from '@/constants/workspace'
import diagramUtils from '@wkeComps/ErdWke/diagramUtils'

const props = defineProps({
  data: {
    type: Object,
    required: true,
    validator: (obj) =>
      ['isOpened', 'type', 'item', 'target'].every((key) => key in obj) &&
      (obj.type === '' || [TYPE.BOARD, TYPE.NODE, TYPE.LINK].includes(obj.type)),
  },
  graphConfig: { type: Object, required: true },
  exportOptions: { type: Object, required: true },
  colKeyCategoryMap: { type: Object, required: true },
})

const emit = defineEmits([
  'open-editor',
  'rm-tbl',
  'rm-fk',
  'create-tbl',
  'fit-into-view',
  'auto-arrange-erd',
  'patch-graph-config',
  'update-cardinality',
])

const typy = useTypy()
const { t } = useI18n()

const { BOARD, NODE, LINK } = TYPE

const item = computed(() => props.data.item)
const activatorId = computed(() => typy(props.data, 'activatorId').safeString)

const boardOpts = computed(() => [
  { title: t('createTable'), action: () => emit('create-tbl') },
  { title: t('fitDiagramInView'), action: () => emit('fit-into-view') },
  { title: t('autoArrangeErd'), action: () => emit('auto-arrange-erd') },
  {
    title: t(
      props.graphConfig.link.isAttrToAttr ? 'disableDrawingFksToCols' : 'enableDrawingFksToCols'
    ),
    action: () =>
      emit('patch-graph-config', {
        path: 'link.isAttrToAttr',
        value: !props.graphConfig.link.isAttrToAttr,
      }),
  },
  {
    title: t(
      props.graphConfig.link.isHighlightAll
        ? 'turnOffRelationshipHighlight'
        : 'turnOnRelationshipHighlight'
    ),

    action: () =>
      emit('patch-graph-config', {
        path: 'link.isHighlightAll',
        value: !props.graphConfig.link.isHighlightAll,
      }),
  },
  { title: t('export'), children: props.exportOptions },
])

const entityOpts = computed(() =>
  Object.values(ENTITY_OPT_TYPE_MAP).map((type) => ({
    type,
    title: t(type),
    action: () => {
      const { EDIT, REMOVE } = ENTITY_OPT_TYPE_MAP
      switch (type) {
        case EDIT:
          emit('open-editor', { node: item.value })
          break
        case REMOVE:
          emit('rm-tbl', item.value)
          break
      }
    },
  }))
)

const linkOpts = computed(() => {
  const opts = [
    {
      title: t(LINK_OPT_TYPE_MAP.EDIT),
      type: LINK_OPT_TYPE_MAP.EDIT,
      action: () =>
        emit('open-editor', {
          node: typy(item.value, 'source').safeObjectOrEmpty,
          spec: TABLE_STRUCTURE_SPEC_MAP.FK,
        }),
    },
    {
      title: t(LINK_OPT_TYPE_MAP.REMOVE),
      type: LINK_OPT_TYPE_MAP.REMOVE,
      action: () => emit('rm-fk', item.value),
    },
  ]
  const link = item.value
  if (link) {
    const actionCb = (type) => emit('update-cardinality', { type, link })
    opts.push(diagramUtils.genCardinalityOpt({ link, actionCb }))
    const {
      relationshipData: { src_attr_id, target_attr_id },
    } = link
    const colKeyCategories = props.colKeyCategoryMap[src_attr_id]
    const refColKeyCategories = props.colKeyCategoryMap[target_attr_id]

    if (!colKeyCategories.includes(CREATE_TBL_TOKEN_MAP.primaryKey))
      opts.push(diagramUtils.genOptionalityOpt({ link, actionCb }))

    if (!refColKeyCategories.includes(CREATE_TBL_TOKEN_MAP.primaryKey))
      opts.push(diagramUtils.genOptionalityOpt({ link, actionCb, isForRefTbl: true }))
  }
  return opts
})

const ctxMenuItems = computed(() => {
  switch (props.data.type) {
    case BOARD:
      return boardOpts.value
    case NODE:
      return entityOpts.value
    case LINK:
      return linkOpts.value
    default:
      return []
  }
})
</script>

<template>
  <CtxMenu
    v-if="activatorId"
    :target="data.target"
    :activator="`#${activatorId}`"
    :items="ctxMenuItems"
    transition="slide-y-transition"
    content-class="full-border"
    @item-click="$event.action()"
  />
</template>
