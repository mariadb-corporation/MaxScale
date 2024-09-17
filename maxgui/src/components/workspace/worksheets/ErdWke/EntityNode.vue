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
import ErdKeyIcon from '@wkeComps/ErdWke/ErdKeyIcon.vue'
import { CREATE_TBL_TOKEN_MAP } from '@/constants/workspace'
import erdHelper from '@/utils/erdHelper'

const props = defineProps({
  node: { type: Object, required: true },
  headerHeight: { type: String, required: true },
  rowHeight: { type: String, required: true },
  colKeyCategoryMap: { type: Object, required: true },
  keyCategoryMap: { type: Object, required: true },
  highlightColStyleMap: { type: Object, required: true },
  isDrawingFk: { type: Boolean, required: true },
})
const emit = defineEmits(['mouseenter-attr', 'mouseleave-attr'])

const COL_MAX_WIDTH = 320 / 2 - 27 // entity max-width / 2 - offset. Offset includes padding and border

const typy = useTypy()

const nodeData = computed(() => typy(props.node, 'data').safeObjectOrEmpty)
const highlightColor = computed(() => typy(props.node, 'styles.highlightColor').safeString)

function getHighlightColStyle(colId) {
  return typy(props.highlightColStyleMap[colId]).safeObjectOrEmpty
}

function getKeyIcon(colId) {
  const { primaryKey, uniqueKey, key, fullTextKey, spatialKey, foreignKey } = CREATE_TBL_TOKEN_MAP
  const { color } = getHighlightColStyle(colId)

  const categories = typy(props.colKeyCategoryMap, `[${colId}]`).safeArray

  let isUQ = false
  if (categories.includes(uniqueKey))
    isUQ = erdHelper.isSingleUQ({ keyCategoryMap: props.keyCategoryMap, colId })

  if (categories.includes(primaryKey))
    return { icon: '$mdiKey', color: color ? color : 'primary', size: 18 }
  else if (isUQ)
    return { icon: 'mxs:uniqueIndexKey', color: color ? color : 'navigation', size: 16 }
  else if ([key, fullTextKey, spatialKey, foreignKey].some((k) => categories.includes(k)))
    return { icon: 'mxs:indexKey', color: color ? color : 'navigation', size: 16 }
}
</script>

<template>
  <VHover>
    <template #default="{ isHovering, props }">
      <table class="entity-table" v-bind="props">
        <thead>
          <tr>
            <th
              class="text-center font-weight-bold text-no-wrap rounded-t-lg pl-4 pr-1"
              colspan="3"
            >
              <div class="d-flex flex-row align-center justify-center">
                <div class="flex-grow-1" data-test="entity-name">
                  {{ nodeData.options.name }}
                </div>
                <slot name="entity-setting-btn" :node="node" :isHovering="isHovering" />
              </div>
            </th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="(col, colId) in nodeData.defs.col_map"
            :key="colId"
            :style="getHighlightColStyle(colId)"
            v-on="
              isDrawingFk
                ? {
                    mouseenter: () => emit('mouseenter-attr', col),
                    mouseleave: () => emit('mouseleave-attr'),
                  }
                : {}
            "
          >
            <td>
              <ErdKeyIcon class="fill-height d-flex align-center" :data="getKeyIcon(colId)" />
            </td>
            <td>
              <GblTooltipActivator
                :data="{ txt: col.name }"
                fillHeight
                :maxWidth="COL_MAX_WIDTH"
                activateOnTruncation
              />
            </td>
            <td
              class="text-end"
              :style="{
                color: $typy(getHighlightColStyle(colId), 'color').safeString || '#6c7c7b',
              }"
            >
              <GblTooltipActivator
                :data="{ txt: col.data_type }"
                fillHeight
                :maxWidth="COL_MAX_WIDTH"
                activateOnTruncation
              />
            </td>
          </tr>
        </tbody>
      </table>
    </template>
  </VHover>
</template>

<style lang="scss" scoped>
.entity-table {
  background: white;
  width: 100%;
  border-spacing: 0px;
  border-color: v-bind(highlightColor);
  tr,
  thead,
  tbody {
    border-color: inherit;
  }
  thead {
    th {
      border-top: 7px solid;
      border-right: 1px solid;
      border-bottom: 1px solid;
      border-left: 1px solid;
      border-color: inherit;
    }
    tr {
      height: v-bind(headerHeight);
    }
  }
  tbody {
    tr {
      height: v-bind(rowHeight);
      &:hover {
        background: colors.$tr-hovered-color;
      }
      td {
        white-space: nowrap;
        padding: 0px 8px;
        &:first-of-type {
          padding-left: 8px;
          padding-right: 0px;
          border-left: 1px solid;
          border-color: inherit;
        }
        &:nth-of-type(2) {
          padding-left: 2px;
        }
        &:last-of-type {
          border-right: 1px solid;
          border-color: inherit;
        }
      }
      &:last-of-type {
        td {
          border-bottom: 1px solid;
          border-color: inherit;
          &:first-of-type {
            border-bottom-left-radius: 8px !important;
          }
          &:last-of-type {
            border-bottom-right-radius: 8px !important;
          }
        }
      }
    }
  }
}
</style>
