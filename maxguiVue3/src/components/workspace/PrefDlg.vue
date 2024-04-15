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
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import RowLimit from '@wkeComps/QueryEditor/RowLimit.vue'
import queryConnService from '@wsServices/queryConnService'
import { MXS_OBJ_TYPES } from '@/constants'
import { PREF_TYPES, OS_KEY, IS_MAC_OS } from '@/constants/workspace'

const attrs = useAttrs()
const emit = defineEmits(['update:modelValue'])

const store = useStore()
const { t } = useI18n()
const typy = useTypy()
const {
  daysDiff,
  addDaysToNow,
  deepDiff,
  lodash: { isEqual, cloneDeep },
} = useHelpers()

const { LISTENERS, SERVERS, SERVICES } = MXS_OBJ_TYPES
const { QUERY_EDITOR, CONN } = PREF_TYPES
const OBJ_CONN_TYPES = [LISTENERS, SERVERS, SERVICES]
const SYS_VAR_REF_LINK = 'https://mariadb.com/docs/server/ref/mdb/system-variables/'

let preferences = ref({})
let tooltip = ref(null)
let activePrefType = ref(null)

const isOpened = computed({
  get: () => attrs.modelValue,
  set: (v) => emit('update:modelValue', v),
})

const persistedPref = computed(() => ({
  query_row_limit: store.state.prefAndStorage.query_row_limit,
  /**
   * Backward compatibility in older versions for query_confirm_flag and  query_show_sys_schemas_flag
   * as the values are stored as either 0 or 1.
   */
  query_confirm_flag: Boolean(store.state.prefAndStorage.query_confirm_flag),
  query_show_sys_schemas_flag: Boolean(store.state.prefAndStorage.query_show_sys_schemas_flag),
  // value is converted to number of days
  query_history_expired_time: daysDiff(store.state.prefAndStorage.query_history_expired_time),
  tab_moves_focus: store.state.prefAndStorage.tab_moves_focus,
  max_statements: store.state.prefAndStorage.max_statements,
  identifier_auto_completion: store.state.prefAndStorage.identifier_auto_completion,
  def_conn_obj_type: store.state.prefAndStorage.def_conn_obj_type,
  interactive_timeout: store.state.prefAndStorage.interactive_timeout,
  wait_timeout: store.state.prefAndStorage.wait_timeout,
}))

const prefFieldMap = computed(() => {
  return {
    [QUERY_EDITOR]: {
      positiveNumber: [
        {
          id: 'query_row_limit',
          label: t('rowLimit'),
          icon: 'mxs:statusWarning',
          iconColor: 'warning',
          iconTooltipTxt: 'info.rowLimit',
        },
        {
          id: 'max_statements',
          label: t('maxStatements'),
          icon: 'mxs:statusWarning',
          iconColor: 'warning',
          iconTooltipTxt: 'info.maxStatements',
        },
        {
          id: 'query_history_expired_time',
          label: t('queryHistoryRetentionPeriod'),
          suffix: t('days'),
        },
      ],
      boolean: [
        { id: 'query_confirm_flag', label: t('showQueryConfirm') },
        { id: 'query_show_sys_schemas_flag', label: t('showSysSchemas') },
        {
          id: 'tab_moves_focus',
          label: t('tabMovesFocus'),
          icon: '$mdiInformationOutline',
          iconColor: 'info',
          iconTooltipTxt: preferences.value.tab_moves_focus
            ? 'info.tabMovesFocus'
            : 'info.tabInsetChar',
          shortcut: `${OS_KEY} ${IS_MAC_OS ? '+ SHIFT' : ''} + M`,
        },
        {
          id: 'identifier_auto_completion',
          label: t('identifierAutoCompletion'),
          icon: '$mdiInformationOutline',
          iconColor: 'info',
          iconTooltipTxt: 'info.identifierAutoCompletion',
        },
      ],
    },
    [CONN]: {
      enum: [
        {
          id: 'def_conn_obj_type',
          label: t('defConnObjType'),
          enumValues: OBJ_CONN_TYPES,
        },
      ],
      positiveNumber: [
        {
          id: 'interactive_timeout',
          label: 'interactive_timeout',
          icon: 'mxs:questionCircle',
          iconColor: 'info',
          href: `${SYS_VAR_REF_LINK}/interactive_timeout/#DETAILS`,
          isVariable: true,
          suffix: 'seconds',
        },
        {
          id: 'wait_timeout',
          label: 'wait_timeout',
          icon: 'mxs:questionCircle',
          iconColor: 'info',
          href: `${SYS_VAR_REF_LINK}/wait_timeout/#DETAILS`,
          isVariable: true,
          suffix: 'seconds',
        },
      ],
    },
  }
})

const hasChanged = computed(() => !isEqual(persistedPref.value, preferences.value))

const activeQueryEditorConnId = computed(
  () => typy(QueryConn.getters('activeQueryEditorConn'), 'id').safeString
)

watch(
  isOpened,
  (v) => {
    if (v) preferences.value = cloneDeep(persistedPref.value)
  },
  { immediate: true }
)

async function onSave() {
  const diffs = deepDiff(persistedPref.value, preferences.value)
  let systemVariables = []
  for (const diff of diffs) {
    const key = typy(diff, 'path[0]').safeString
    let value = diff.rhs
    switch (key) {
      case 'query_history_expired_time':
        // Convert back to unix timestamp
        value = addDaysToNow(value)
        break
      case 'interactive_timeout':
      case 'wait_timeout':
        systemVariables.push(key)
        break
    }
    store.commit(`prefAndStorage/SET_${key.toUpperCase()}`, value)
  }
  if (activeQueryEditorConnId.value && systemVariables.length)
    await queryConnService.setVariables({
      connId: activeQueryEditorConnId.value,
      config: Worksheet.getters('activeRequestConfig'),
      variables: systemVariables,
    })
}
</script>

<template>
  <BaseDlg
    v-model="isOpened"
    :onSave="onSave"
    :title="$t('pref')"
    :lazyValidation="false"
    minBodyWidth="800px"
    :saveDisabled="!hasChanged"
    bodyCtrClass="px-0 pb-4"
    formClass="px-12 py-0"
  >
    <template #form-body>
      <div class="d-flex">
        <VTabs v-model="activePrefType" direction="vertical" class="v-tabs--vert" hide-slider>
          <VTab
            v-for="(item, type) in prefFieldMap"
            :key="type"
            :value="type"
            class="justify-space-between align-center"
          >
            <div class="tab-name pa-2 text-navigation font-weight-regular">
              {{ type }}
            </div>
          </VTab>
        </VTabs>
        <VWindow v-if="!$typy(preferences).isEmptyObject" v-model="activePrefType" class="w-100">
          <VWindowItem v-for="(item, type) in prefFieldMap" :key="type" :value="type" class="w-100">
            <div class="pl-4 pr-2 overflow-y-auto pref-fields-ctr">
              <template v-for="(fields, type) in item">
                <template v-for="field in fields">
                  <CnfField
                    v-if="!$typy(preferences[field.id]).isNull"
                    :key="field.id"
                    v-model="preferences[field.id]"
                    :type="type"
                    :field="field"
                    :class="{ 'mt-1': type === 'boolean' }"
                    @tooltip="tooltip = $event"
                  >
                    <template v-if="field.id === 'query_row_limit'" #[`${field.id}-input`]>
                      <RowLimit
                        v-model="preferences[field.id]"
                        :id="field.id"
                        hide-details="auto"
                      />
                    </template>
                  </CnfField>
                </template>
              </template>
              <small v-if="type === PREF_TYPES.CONN" class="mt-auto pt-2">
                {{ $t('info.timeoutVariables') }}
              </small>
            </div>
          </VWindowItem>
        </VWindow>
        <VTooltip
          v-if="$typy(tooltip, 'activator').safeString"
          :model-value="Boolean(tooltip)"
          location="top"
          transition="slide-y-transition"
          :activator="tooltip.activator"
          max-width="400"
        >
          <i18n-t :keypath="$typy(tooltip, 'iconTooltipTxt').safeString" tag="span" scope="global">
            <template v-if="tooltip.shortcut" #shortcut>
              <b> {{ tooltip.shortcut }} </b>
            </template>
          </i18n-t>
        </VTooltip>
      </div>
    </template>
  </BaseDlg>
</template>

<style lang="scss" scoped>
.pref-fields-ctr {
  min-height: 360px;
  max-height: 520px;
}
</style>
