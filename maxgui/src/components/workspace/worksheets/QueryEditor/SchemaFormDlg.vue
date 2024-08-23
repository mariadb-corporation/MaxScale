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
import schemaInfoService from '@wsServices/schemaInfoService'
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import ComboboxWithDef from '@wsComps/ComboboxWithDef.vue'
import { exeSql } from '@/store/queryHelper'

const props = defineProps({ data: { type: Object }, isAltering: { type: Boolean, default: false } })

const attrs = useAttrs()

const store = useStore()
const { t } = useI18n()
const typy = useTypy()
const {
  lodash: { cloneDeep, isEqual },
  escapeSingleQuote,
  quotingIdentifier,
} = useHelpers()
const { validateRequired } = useValidationRule()

const form = ref({
  name: 'new_schema',
  charset: 'utf8mb4',
  collation: 'utf8mb4_general_ci',
  comment: '',
})
const formValidity = ref(null)
const resultErr = ref(null)

const activeQueryTabConnId = computed(
  () => typy(QueryConn.getters('activeQueryTabConn'), 'id').safeString
)
const activeRequestConfig = computed(() => Worksheet.getters('activeRequestConfig'))
const charset_collation_map = computed(() => store.state.schemaInfo.charset_collation_map)
const def_db_charset_map = computed(() => store.state.schemaInfo.def_db_charset_map)
const defCollation = computed(
  () => typy(charset_collation_map.value, `[${form.value.charset}].defCollation`).safeString
)
const defDbCharset = computed(
  () => typy(def_db_charset_map.value, `${typy(form.value, 'name').safeString}`).safeString
)
const charsets = computed(() => Object.keys(charset_collation_map.value))
const collations = computed(() => {
  return typy(charset_collation_map.value, `[${form.value.charset}].collations`).safeArray
})

const saveText = computed(() => (props.isAltering ? 'alter' : 'create'))
const title = computed(() => t(props.isAltering ? 'alterSchema' : 'createSchema'))
const hasChanged = computed(() => !isEqual(props.data, form.value))
const hasError = computed(() => !typy(resultErr.value).isNull)
const saveDisabled = computed(() => (props.isAltering ? !hasChanged.value : false))

const sql = computed(() => {
  const { name, charset, collation, comment } = form.value
  const escapedComment = `'${escapeSingleQuote(comment)}'`
  const identifier = quotingIdentifier(name)
  const action = props.isAltering ? 'ALTER' : 'CREATE'

  let str = `${action} SCHEMA ${identifier}`
  if (charset) str += `\n\tCHARACTER SET = ${charset}`
  if (collation) str += `\n\tCOLLATE = ${collation}`
  if (comment || props.isAltering) str += `\n\tCOMMENT = ${escapedComment} `

  return str
})
watch(
  () => attrs.modelValue,
  async (v) => {
    if (v)
      await schemaInfoService.querySuppData({
        connId: activeQueryTabConnId.value,
        config: activeRequestConfig.value,
      })
    else {
      formValidity.value = null
      resultErr.value = null
    }
  }
)

watch(
  () => props.data,
  (v) => {
    if (v) form.value = cloneDeep(v)
  },
  { immediate: true }
)

watch(form, () => (resultErr.value = null), { deep: true })

async function exe() {
  const [err] = await exeSql({
    connId: activeQueryTabConnId.value,
    sql: sql.value,
    action: title.value,
  })
  if (err) resultErr.value = err
}
</script>
<template>
  <BaseDlg
    minBodyWidth="624px"
    :title="title"
    :onSave="exe"
    :saveText="saveText"
    :saveDisabled="saveDisabled"
    :hasSavingErr="hasError"
    :allowEnterToSubmit="false"
    :lazyValidation="false"
    @is-form-valid="formValidity = $event"
  >
    <template #form-body>
      <VContainer fluid class="ma-0 pa-0">
        <VRow>
          <VCol cols="12" class="pt-0 pb-1">
            <label class="label-field text-small-text label--required" for="schema">
              {{ t('schema') }}
            </label>
            <!-- Name is not alterable and is disabled in altering mode -->
            <VTextField
              id="schema"
              data-test="schema"
              v-model="form.name"
              hide-details="auto"
              required
              :disabled="isAltering"
              :rules="[validateRequired]"
            />
          </VCol>
          <VCol cols="6" class="py-1">
            <label
              class="label-field text-small-text"
              :class="{ 'label--required': isAltering }"
              for="charset"
            >
              {{ $t('charset') }}
            </label>
            <ComboboxWithDef
              v-model="form.charset"
              id="charset"
              data-test="charset"
              :items="charsets"
              :defItem="defDbCharset"
              :rules="isAltering ? [validateRequired] : []"
              @update:modelValue="form.collation = defCollation"
            />
          </VCol>
          <VCol cols="6" class="py-1">
            <label
              class="label-field text-small-text"
              :class="{ 'label--required': isAltering }"
              for="collation"
            >
              {{ $t('collation') }}
            </label>
            <ComboboxWithDef
              v-model="form.collation"
              id="collation"
              data-test="collation"
              :items="collations"
              :defItem="defCollation"
              :rules="isAltering ? [validateRequired] : []"
            />
          </VCol>
          <VCol cols="12" class="py-1">
            <label class="label-field text-small-text" for="comment">
              {{ $t('comment') }}
            </label>
            <VTextarea
              v-model="form.comment"
              id="comment"
              data-test="comment"
              auto-grow
              :rows="2"
              row-height="15"
              autocomplete="off"
              hide-details="auto"
            />
          </VCol>
          <VCol v-if="formValidity" cols="12">
            <h6 class="text-h6 text-navigation">{{ $t('sqlPrvw') }}</h6>
            <div :style="{ height: '120px' }" class="pt-2 bg-separator">
              <SqlEditor
                :modelValue="sql"
                :options="{
                  fontSize: 10,
                  contextmenu: false,
                  lineNumbers: 'off',
                  folding: false,
                  lineNumbersMinChars: 0,
                  lineDecorationsWidth: 12,
                }"
                skipRegCompleters
                readOnly
                class="fill-height"
              />
            </div>
          </VCol>
          <VCol v-if="hasError">
            <table class="tbl-code pa-4" data-test="result-error-tbl">
              <tr v-for="(v, key) in resultErr" :key="key">
                <td>
                  <b>{{ key }}</b>
                </td>
                <td>{{ v }}</td>
              </tr>
            </table>
          </VCol>
        </VRow>
      </VContainer>
    </template>
  </BaseDlg>
</template>
