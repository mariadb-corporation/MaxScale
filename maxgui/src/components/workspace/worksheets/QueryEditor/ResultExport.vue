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
import {
  CsvExporter,
  JsonExporter,
  SqlExporter,
  SQL_EXPORT_OPTS,
} from '@wkeComps/QueryEditor/resultExporter'

const props = defineProps({
  fields: { type: Array, required: true },
  rows: { type: Array, required: true },
  defExportFileName: { type: String, required: true },
  exportAsSQL: { type: Boolean, required: true },
  metadata: { type: Array, required: true },
})

const { t } = useI18n()
const typy = useTypy()
const { dateFormat, escapeBackslashes } = useHelpers()
const logger = useLogger()
const { validateRequired } = useValidationRule()

const isFormValid = ref(false)
const isConfigDialogOpened = ref(false)
const selectedFormat = ref(null)
const excludedFieldIndexes = ref([])
const fileName = ref('')
const csvOpts = ref({
  fieldsTerminatedBy: '',
  linesTerminatedBy: '',
  nullReplacedBy: '',
  withHeaders: false,
})

const chosenSqlOpt = ref(SQL_EXPORT_OPTS.BOTH)

const fileFormats = computed(() =>
  [
    {
      contentType: 'data:text/csv;charset=utf-8;',
      extension: 'csv',
    },
    {
      contentType: 'data:application/json;charset=utf-8;',
      extension: 'json',
    },
  ].concat(
    props.exportAsSQL
      ? {
          contentType: 'data:application/sql;charset=utf-8;',
          extension: 'sql',
        }
      : []
  )
)
const sqlExportOpts = computed(() =>
  Object.keys(SQL_EXPORT_OPTS).map((key) => ({
    title: t(SQL_EXPORT_OPTS[key]),
    value: SQL_EXPORT_OPTS[key],
  }))
)
const selectedFields = computed(() =>
  props.fields.reduce((acc, field, i) => {
    if (!excludedFieldIndexes.value.includes(i)) acc.push({ value: field, index: i })
    return acc
  }, [])
)
const totalSelectedFields = computed(() => selectedFields.value.length)
const selectedFieldsLabel = computed(() => {
  const firstSelectedField = typy(selectedFields.value, '[0].value').safeString
  if (totalSelectedFields.value > 1)
    return `${firstSelectedField} (+${totalSelectedFields.value - 1} others)`
  return firstSelectedField
})
const selectedFieldsErrMsg = computed(() =>
  totalSelectedFields.value ? '' : t('errors.requiredField')
)

watch(isConfigDialogOpened, () => assignDefOpt())

function assignDefOpt() {
  excludedFieldIndexes.value = []
  //TODO: Determine OS newline and store it as user preferences
  // escape reserved single character escape sequences so it can be rendered to the DOM
  csvOpts.value = {
    fieldsTerminatedBy: '\\t',
    linesTerminatedBy: '\\n',
    nullReplacedBy: '\\N',
    withHeaders: false,
  }
  selectedFormat.value = fileFormats.value[0] // csv
}

/**
 * For CSV option inputs
 * Input entered by the user is escaped automatically.
 * As the result, if the user enters \t, it is escaped as \\t. However, here
 * we allow the user to add the custom line | fields terminator, so when the user
 * enters \t, it should be parsed as a tab character. At the moment, JS doesn't
 * allow to have dynamic escaped char. So this function uses JSON.parse approach
 * to unescaped inputs
 * @param {String} v - users utf8 input
 */
function unescapedTerminatedChar(v) {
  try {
    let str = v
    // if user enters \\, escape it again so it won't be removed when it is parsed by JSON.parse
    if (str.includes('\\\\')) str = escapeBackslashes(str)
    return JSON.parse(
      '"' +
        str.replace(/"/g, '\\"') + // escape " to prevent json syntax errors
        '"'
    )
  } catch (e) {
    logger.error(e)
  }
}

/**
 * @param {string} fileExtension
 * @return {string}
 */
function getData(fileExtension) {
  const data = { fields: selectedFields.value, data: props.rows }
  switch (fileExtension) {
    case 'json':
      return new JsonExporter(data).export()
    case 'csv':
      return new CsvExporter({
        ...data,
        opts: {
          fieldsTerminatedBy: unescapedTerminatedChar(csvOpts.value.fieldsTerminatedBy),
          linesTerminatedBy: unescapedTerminatedChar(csvOpts.value.linesTerminatedBy),
          nullReplacedBy: csvOpts.value.nullReplacedBy,
          withHeaders: csvOpts.value.withHeaders,
        },
      }).export()
    case 'sql':
      return new SqlExporter({
        ...data,
        metadata: props.metadata,
        opt: chosenSqlOpt.value,
      }).export()
  }
}

function getDefFileName() {
  return `${props.defExportFileName} - ${dateFormat({ value: new Date() })}`
}

function openConfigDialog() {
  isConfigDialogOpened.value = !isConfigDialogOpened.value
  fileName.value = getDefFileName()
}

function onExport() {
  const { contentType, extension } = selectedFormat.value
  const a = document.createElement('a')
  a.href = `${contentType},${encodeURIComponent(getData(extension))}`
  a.download = `${fileName.value}.${extension}`
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
}
</script>

<template>
  <TooltipBtn square variant="text" size="small" color="primary" @click="openConfigDialog">
    <template #btn-content>
      <VIcon size="16" icon="$mdiDownload" />
      <BaseDlg
        v-model="isConfigDialogOpened"
        :onSave="onExport"
        :title="$t('exportResults')"
        saveText="export"
        minBodyWidth="512px"
        :lazyValidation="false"
        @is-form-valid="isFormValid = $event"
      >
        <template #form-body>
          <VContainer class="pa-1">
            <VRow class="ma-n1">
              <VCol cols="12" class="pa-1">
                <LabelField
                  v-model="fileName"
                  :label="$t('fileName')"
                  required
                  hide-details="auto"
                />
              </VCol>
              <VCol cols="12" class="pa-1">
                <label class="label-field text-small-text label--required" for="fields-to-export">
                  {{ $t('fieldsToExport') }}
                </label>
                <FilterList
                  v-model="excludedFieldIndexes"
                  :items="fields"
                  reverse
                  :maxHeight="400"
                  returnIndex
                  :maxWidth="380"
                >
                  <template #activator="{ data: { props, isActive } }">
                    <div v-bind="props">
                      <VTextField
                        :modelValue="selectedFieldsLabel"
                        readonly
                        :error="Boolean(selectedFieldsErrMsg)"
                        :error-messages="selectedFieldsErrMsg"
                        hide-details="auto"
                        id="fields-to-export"
                      >
                        <template #append-inner>
                          <VIcon
                            size="10"
                            :color="isActive ? 'primary' : ''"
                            class="cursor--pointer"
                            :class="[isActive ? 'rotate-up' : 'rotate-down']"
                            icon="mxs:menuDown"
                          />
                        </template>
                      </VTextField>
                    </div>
                  </template>
                </FilterList>
              </VCol>
            </VRow>
            <VDivider class="my-4" />
            <VRow class="ma-n1">
              <VCol cols="12" class="pa-1">
                <label class="label-field text-small-text" for="file-format">
                  {{ $t('fileFormat') }}
                </label>
                <VSelect
                  v-model="selectedFormat"
                  :items="fileFormats"
                  return-object
                  item-title="extension"
                  item-value="contentType"
                  :rules="[validateRequired]"
                  hide-details="auto"
                  id="file-format"
                />
              </VCol>
            </VRow>
            <template v-if="$typy(selectedFormat, 'extension').safeString === 'csv'">
              <VRow class="ma-n1">
                <VCol v-for="(_, key) in csvOpts" :key="key" cols="12" md="12" class="pa-1">
                  <VCheckboxBtn
                    v-if="key === 'withHeaders'"
                    v-model="csvOpts[key]"
                    class="ml-n2"
                    :label="$t(key)"
                  />
                  <LabelField
                    v-else
                    v-model="csvOpts[key]"
                    :label="$t(key)"
                    required
                    hide-details="auto"
                  />
                </VCol>
              </VRow>
            </template>
            <template v-if="$typy(selectedFormat, 'extension').safeString === 'sql'">
              <VRow class="mt-3 mx-n1 mb-n1">
                <VCol cols="12" md="12" class="pa-1">
                  <label class="label-field text-small-text" for="sql-opt">
                    {{ $t('exportOpt') }}
                  </label>
                  <VTooltip location="top">
                    <template #activator="{ props }">
                      <VIcon
                        class="ml-1 cursor--pointer"
                        size="14"
                        color="primary"
                        icon="mxs:questionCircle"
                        v-bind="props"
                      />
                    </template>
                    <table>
                      <tr v-for="(v, key) in ['data', 'structure']" :key="`${key}`">
                        <td>{{ $t(v) }}:</td>
                        <td class="font-weight-bold pl-1">
                          {{ $t(`info.sqlExportOpt.${v}`) }}
                        </td>
                      </tr>
                    </table>
                  </VTooltip>
                  <VSelect
                    v-model="chosenSqlOpt"
                    :items="sqlExportOpts"
                    hide-details="auto"
                    id="sql-opt"
                  />
                </VCol>
              </VRow>
            </template>
          </VContainer>
        </template>
      </BaseDlg>
    </template>
    {{ $t('exportResults') }}
  </TooltipBtn>
</template>
