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
import Worksheet from '@wsModels/Worksheet'
import {
  EVENT_PROVIDER_KEY,
  QUERY_CONN_BINDING_TYPES,
  QUERY_SHORTCUT_KEYS,
  MIGR_DLG_TYPES,
} from '@/constants/workspace'
import WkeNavCtr from '@wsComps/WkeNavCtr.vue'
import '@/styles/workspace.scss'

const props = defineProps({
  disableRunQueries: { type: Boolean, default: false },
  disableDataMigration: { type: Boolean, default: false },
  runQueriesSubtitle: { type: String, default: '' },
  dataMigrationSubtitle: { type: String, default: '' },
})

let dim = ref({})
let ctrRef = ref(null)
let shortkey = ref(null)

const store = useStore()
const { t } = useI18n()
const typy = useTypy()

const is_fullscreen = computed(() => store.state.prefAndStorage.is_fullscreen)
const is_validating_conn = computed(() => store.state.queryConnsMem.is_validating_conn)
const hidden_comp = computed(() => store.state.mxsWorkspace.hidden_comp)

const keptAliveWorksheets = computed(() => {
  return Worksheet.query()
    .where((wke) => isQueryEditorWke(wke) || isEtlWke(wke) || isErdWke(wke))
    .get()
})
const activeWkeId = computed(() => Worksheet.getters('activeId'))
const activeWke = computed(() => Worksheet.getters('activeRecord'))
const wkeNavCtrHeight = computed(() => (hidden_comp.value.includes('wke-nav-ctr') ? 0 : 32))
const ctrDim = computed(() => ({
  width: dim.value.width,
  height: dim.value.height - wkeNavCtrHeight.value,
}))
const blankWkeCards = computed(() => [
  {
    title: t('runQueries'),
    subtitle: props.runQueriesSubtitle,
    icon: 'mxs:workspace',
    iconSize: 26,
    disabled: props.disableRunQueries,
    click: () =>
      store.commit('mxsWorkspace/SET_CONN_DLG', {
        is_opened: true,
        type: QUERY_CONN_BINDING_TYPES.QUERY_EDITOR,
      }),
  },
  {
    title: t('dataMigration'),
    subtitle: props.dataMigrationSubtitle,
    icon: 'mxs:dataMigration',
    iconSize: 32,
    disabled: props.disableDataMigration,
    click: () =>
      store.commit('mxsWorkspace/SET_MIGR_DLG', { type: MIGR_DLG_TYPES.CREATE, is_opened: true }),
  },
  {
    title: t('createAnErd'),
    icon: 'mxs:erd',
    iconSize: 32,
    click: () =>
      store.commit('mxsWorkspace/SET_CONN_DLG', {
        is_opened: true,
        type: QUERY_CONN_BINDING_TYPES.ERD,
      }),
  },
])

onBeforeMount(() => store.dispatch('prefAndStorage/handleAutoClearQueryHistory'))
onMounted(() => nextTick(() => setDim()))
function setDim() {
  const { width, height } = ctrRef.value.getBoundingClientRect()
  dim.value = { width, height }
}

function isErdWke(wke) {
  return Boolean(typy(wke, 'erd_task_id').safeString)
}

function isEtlWke(wke) {
  return Boolean(typy(wke, 'etl_task_id').safeString)
}

function isQueryEditorWke(wke) {
  return Boolean(typy(wke, 'query_editor_id').safeString)
}

function isBlankWke(wke) {
  return !isQueryEditorWke(wke) && !isEtlWke(wke) && !isErdWke(wke)
}

provide(EVENT_PROVIDER_KEY, shortkey)
</script>

<template>
  <div
    ref="ctrRef"
    v-resize.quiet="setDim"
    v-shortkey="QUERY_SHORTCUT_KEYS"
    class="workspace-ctr fill-height"
    @shortkey="shortkey = $event.srcKey"
  >
    <div
      class="fill-height d-flex flex-column"
      :class="{ 'workspace-ctr--fullscreen': is_fullscreen }"
    >
      <VProgressLinear v-if="is_validating_conn" indeterminate color="primary" />
      <!-- TODO: Migrate sub components -->
      <template v-else>
        <WkeNavCtr v-if="!hidden_comp.includes('wke-nav-ctr')" :height="wkeNavCtrHeight" />
      </template>
    </div>
  </div>
</template>

<style lang="scss" scoped>
.workspace-ctr {
  &--fullscreen {
    background: white;
    z-index: 9999;
    position: fixed;
    top: 0px;
    right: 0px;
    bottom: 0px;
    left: 0px;
  }
}
</style>
