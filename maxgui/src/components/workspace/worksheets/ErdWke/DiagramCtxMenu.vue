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
import { ENTITY_OPT_TYPE_MAP, LINK_OPT_TYPE_MAP, CREATE_TBL_TOKEN_MAP } from '@/constants/workspace'
import diagramUtils from '@wkeComps/ErdWke/diagramUtils'

const props = defineProps({
  type: {
    type: String,
    required: true,
    validator: (v) => v === '' || [TYPE.BOARD, TYPE.NODE, TYPE.LINK].includes(v),
  },
  graphConfig: { type: Object, required: true },
  exportOptions: { type: Object, required: true },
  colKeyCategoryMap: { type: Object, required: true },
  activeCtxItem: { type: Object, required: true },
  activeCtxItemId: { type: String, required: true },
})

const emit = defineEmits([
  'edit-tbl',
  'rm-tbl',
  'edit-fk',
  'rm-fk',
  'create-tbl',
  'fit-into-view',
  'auto-arrange-erd',
  'patch-graph-config',
  'update-cardinality',
])

const { t } = useI18n()

const { BOARD, NODE, LINK } = TYPE

const ENTITY_OPTS = Object.values(ENTITY_OPT_TYPE_MAP).map((type) => ({
  type,
  title: t(type),
  action: () => {
    const { EDIT, REMOVE } = ENTITY_OPT_TYPE_MAP
    switch (type) {
      case EDIT:
        emit('edit-tbl')
        break
      case REMOVE:
        emit('rm-tbl')
        break
    }
  },
}))

const FK_BASE_OPTS = [
  {
    title: t(LINK_OPT_TYPE_MAP.EDIT),
    type: LINK_OPT_TYPE_MAP.EDIT,
    action: () => emit('edit-fk'),
  },
  {
    title: t(LINK_OPT_TYPE_MAP.REMOVE),
    type: LINK_OPT_TYPE_MAP.REMOVE,
    action: () => emit('rm-fk'),
  },
]

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

const linkCtxOpts = computed(() => {
  const opts = []
  const link = props.activeCtxItem
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

const linkOpts = computed(() => [...FK_BASE_OPTS, ...linkCtxOpts.value])

const ctxMenuItems = computed(() => {
  switch (props.type) {
    case BOARD:
      return boardOpts.value
    case NODE:
      return ENTITY_OPTS
    case LINK:
      return linkOpts.value
    default:
      return []
  }
})
</script>

<template>
  <CtxMenu
    v-if="activeCtxItemId"
    :key="activeCtxItemId"
    :activator="`#${activeCtxItemId}`"
    :items="ctxMenuItems"
    transition="slide-y-transition"
    content-class="full-border"
    @item-click="$event.action()"
  />
</template>
