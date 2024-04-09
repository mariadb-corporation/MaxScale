<script setup>
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
import ConnectionBtn from '@wsComps/ConnectionBtn.vue'
import { LINK_SHAPES } from '@/components/svgGraph/shapeConfig'
import {
  QUERY_CONN_BINDING_TYPES,
  ERD_ZOOM_OPTS,
  OS_KEY,
  WS_EMITTER_KEY,
} from '@/constants/workspace'

const props = defineProps({
  graphConfig: { type: Object, required: true },
  height: { type: Number, required: true },
  zoom: { type: Number, required: true },
  isFitIntoView: { type: Boolean, required: true },
  exportOptions: { type: Array, required: true },
  conn: { type: Object, required: true },
  nodesHistory: { type: Array, required: true },
  activeHistoryIdx: { type: Number, required: true },
})
const emit = defineEmits([
  'set-zoom', // { v:number, isFitIntoView: boolean }
  'on-undo',
  'on-redo',
  'on-apply-script',
  'change-graph-config-attr-value', // { path: string, value: any}. path. e.g. 'link.isAttrToAttr'
  'click-auto-arrange',
  'on-create-table',
])

const ALL_LINK_SHAPES = Object.values(LINK_SHAPES)
const BTN_HEIGHT = 28
const store = useStore()
const { t } = useI18n()
const typy = useTypy()

const wsEventListener = inject(WS_EMITTER_KEY)

const zoomValue = computed({
  get: () => Math.floor(props.zoom * 100),
  set: (v) => {
    if (v) emit('set-zoom', { v: v / 100 })
  },
})
const hasConnId = computed(() => Boolean(props.conn.id))
const isUndoDisabled = computed(() => props.activeHistoryIdx === 0)
const isRedoDisabled = computed(() => props.activeHistoryIdx === props.nodesHistory.length - 1)

let unwatch_wsEventListener

onActivated(() => {
  unwatch_wsEventListener = watch(wsEventListener, (v) => shortKeyHandler(v.event))
})

onDeactivated(() => typy(unwatch_wsEventListener).safeFunction())
onBeforeUnmount(() => typy(unwatch_wsEventListener).safeFunction())

function genErd() {
  store.commit('mxsWorkspace/SET_GEN_ERD_DLG', {
    is_opened: true,
    preselected_schemas: [],
    connection: props.conn,
    gen_in_new_ws: false,
  })
}

function handleShowSelection() {
  return `${props.isFitIntoView ? t('fit') : `${zoomValue.value}%`}`
}

function shortKeyHandler(key) {
  switch (key) {
    case 'ctrl-z':
    case 'mac-cmd-z':
      if (!isUndoDisabled.value) emit('on-undo')
      break
    case 'ctrl-shift-z':
    case 'mac-cmd-shift-z':
      if (!isRedoDisabled.value) emit('on-redo')
      break
    case 'ctrl-shift-enter':
    case 'mac-cmd-shift-enter':
      emit('on-apply-script')
      break
  }
}

function openCnnDlg() {
  store.commit('mxsWorkspace/SET_CONN_DLG', { is_opened: true, type: QUERY_CONN_BINDING_TYPES.ERD })
}
</script>

<template>
  <div
    class="er-toolbar-ctr d-flex align-center pr-3 border-bottom--table-border"
    :style="{ minHeight: `${height}px`, maxHeight: `${height}px` }"
  >
    <VTooltip location="top" transition="slide-y-transition">
      <template #activator="{ props }">
        <VSelect
          :modelValue="graphConfig.linkShape.type"
          :items="ALL_LINK_SHAPES"
          class="borderless-select link-shape-select"
          density="compact"
          hide-details
          @update:modelValue="
            emit('change-graph-config-attr-value', { path: 'linkShape.type', value: $event })
          "
          v-bind="props"
        >
          <template #selection="{ item }">
            <VIcon
              size="28"
              color="primary"
              :icon="`mxs:${$helpers.lodash.camelCase(item.raw)}Shape`"
            />
          </template>
          <template #item="{ props }">
            <VListItem v-bind="props">
              <template #title="{ title }">
                <VIcon
                  size="28"
                  color="primary"
                  :icon="`mxs:${$helpers.lodash.camelCase(title)}Shape`"
                />
              </template>
            </VListItem>
          </template>
        </VSelect>
      </template>
      {{ $t('shapeOfLinks') }}
    </VTooltip>
    <TooltipBtn
      class="ml-1 toolbar-square-btn"
      :variant="graphConfig.link.isAttrToAttr ? 'flat' : 'text'"
      color="primary"
      @click="
        emit('change-graph-config-attr-value', {
          path: 'link.isAttrToAttr',
          value: !graphConfig.link.isAttrToAttr,
        })
      "
    >
      <template #btn-content>
        <VIcon size="22" icon="$mdiKeyLink" />
      </template>
      {{ $t(graphConfig.link.isAttrToAttr ? 'disableDrawingFksToCols' : 'enableDrawingFksToCols') }}
    </TooltipBtn>
    <TooltipBtn
      class="toolbar-square-btn"
      variant="text"
      color="primary"
      @click="emit('click-auto-arrange')"
    >
      <template #btn-content>
        <VIcon size="22" icon="$mdiArrangeSendToBack" />
      </template>
      {{ $t('autoArrangeErd') }}
    </TooltipBtn>
    <TooltipBtn
      class="toolbar-square-btn mr-1"
      :variant="graphConfig.link.isHighlightAll ? 'flat' : 'text'"
      color="primary"
      @click="
        emit('change-graph-config-attr-value', {
          path: 'link.isHighlightAll',
          value: !graphConfig.link.isHighlightAll,
        })
      "
    >
      <template #btn-content>
        <VIcon
          size="22"
          :icon="
            graphConfig.link.isHighlightAll ? '$mdiLightbulbOnOutline' : ' $mdiLightbulbOutline'
          "
        />
      </template>
      {{
        $t(
          graphConfig.link.isHighlightAll
            ? 'turnOffRelationshipHighlight'
            : 'turnOnRelationshipHighlight'
        )
      }}
    </TooltipBtn>
    <VTooltip location="top" transition="slide-y-transition">
      <template #activator="{ props }">
        <VSelect
          v-model.number="zoomValue"
          :items="ERD_ZOOM_OPTS"
          class="borderless-select zoom-select"
          density="compact"
          hide-details
          :maxlength="3"
          :placeholder="handleShowSelection()"
          @keypress="$helpers.preventNonNumericalVal($event)"
          v-bind="props"
        >
          <template #prepend-item>
            <VListItem link @click="emit('set-zoom', { isFitIntoView: true })">
              {{ $t('fit') }}
            </VListItem>
          </template>
          <template #selection> {{ handleShowSelection() }} </template>
          <template #item="{ props }">
            <VListItem v-bind="props">
              <template #title="{ title }"> {{ `${title}%` }} </template>
            </VListItem>
          </template>
        </VSelect>
      </template>
      {{ $t('zoom') }}
    </VTooltip>
    <VDivider class="align-self-center er-toolbar__separator mx-2" vertical />
    <TooltipBtn
      class="toolbar-square-btn"
      variant="text"
      color="primary"
      :disabled="isUndoDisabled"
      @click="emit('on-undo')"
    >
      <template #btn-content>
        <VIcon size="22" color="primary" icon="$mdiUndo" />
      </template>
      {{ $t('undo') }}
      <br />
      <kbd>{{ OS_KEY }} + Z</kbd>
    </TooltipBtn>
    <TooltipBtn
      class="toolbar-square-btn"
      variant="text"
      color="primary"
      :disabled="isRedoDisabled"
      @click="emit('on-redo')"
    >
      <template #btn-content>
        <VIcon size="22" color="primary" icon="$mdiRedo" />
      </template>
      {{ $t('redo') }}
      <br />
      <kbd>{{ OS_KEY }} + SHIFT + Z</kbd>
    </TooltipBtn>
    <VDivider class="align-self-center er-toolbar__separator mx-2" vertical />
    <TooltipBtn
      class="toolbar-square-btn"
      variant="text"
      color="primary"
      @click="emit('on-create-table')"
    >
      <template #btn-content>
        <VIcon size="22" color="primary" icon="$mdiTablePlus" />
      </template>
      {{ $t('createTable') }}
    </TooltipBtn>
    <VMenu location="bottom" content-class="with-shadow-border--none">
      <template #activator="{ props }">
        <VTooltip top transition="slide-y-transition">
          <template #activator="{ props: tooltipProps }">
            <VBtn
              variant="text"
              color="primary"
              class="toolbar-square-btn"
              v-bind="{ ...props, ...tooltipProps }"
            >
              <VIcon size="20" icon="$mdiDownload" />
            </VBtn>
          </template>
          {{ $t('export') }}
        </VTooltip>
      </template>
      <VList>
        <VListItem v-for="opt in exportOptions" :key="opt.title" @click="opt.action">
          <VListItemTitle class="text-text"> {{ opt.title }} </VListItemTitle>
        </VListItem>
      </VList>
    </VMenu>
    <TooltipBtn
      class="toolbar-square-btn"
      variant="text"
      color="primary"
      :disabled="!hasConnId"
      @click="emit('on-apply-script')"
    >
      <template #btn-content>
        <VIcon size="20" icon="mxs:running" />
      </template>
      {{ $t('applyScript') }}
      <br />
      <kbd>{{ OS_KEY }} + SHIFT + ENTER</kbd>
    </TooltipBtn>
    <VSpacer />
    <TooltipBtn
      class="toolbar-square-btn"
      variant="text"
      :disabled="!hasConnId"
      :color="hasConnId ? 'primary' : ''"
      @click="genErd"
    >
      <template #btn-content>
        <VIcon size="20" icon="mxs:reports" />
      </template>
      {{ $t('genErd') }}
    </TooltipBtn>
    <ConnectionBtn
      class="connection-btn rounded-0"
      :height="BTN_HEIGHT"
      :activeConn="conn"
      @click="openCnnDlg"
    />
  </div>
</template>

<style lang="scss" scoped>
.er-toolbar-ctr {
  z-index: 4;
  .er-toolbar__separator {
    min-height: 28px;
    max-height: 28px;
  }
  .borderless-select {
    :deep(.v-input__control) {
      .v-field {
        padding-right: 0;
      }
      .v-field__input {
        padding-left: 8px;
      }
      .v-field__outline {
        &__start,
        &__end {
          visibility: hidden;
        }
      }
      .v-field--focused {
        .v-field__outline {
          &__start,
          &__end {
            visibility: visible;
            border-radius: 0px;
          }
        }
      }
    }
  }

  .link-shape-select {
    max-width: 64px;
  }
  .zoom-select {
    max-width: 76px;
  }
}
</style>
