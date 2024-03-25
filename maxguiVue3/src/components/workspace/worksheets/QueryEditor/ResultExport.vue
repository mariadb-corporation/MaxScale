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
import SqlCommenter from '@/utils/SqlCommenter.js'
import { formatSQL } from '@/utils/queryUtils'
import { watch } from 'vue'

const props = defineProps({
  fields: { type: Array, required: true },
  rows: { type: Array, required: true },
  defExportFileName: { type: String, required: true },
  exportAsSQL: { type: Boolean, required: true },
  metadata: { type: Array, required: true },
})

// values are used for i18n
const SQL_EXPORT_OPTS = Object.freeze({
  STRUCTURE: 'structure',
  DATA: 'data',
  BOTH: 'bothStructureAndData',
})

const sqlCommenter = new SqlCommenter()

const { t } = useI18n()
const typy = useTypy()
const {
  quotingIdentifier,
  lodash: { uniq, keyBy },
  dateFormat,
} = useHelpers()
const logger = useLogger()

const isFormValid = ref(false)
const isConfigDialogOpened = ref(false)
const selectedFormat = ref(null)
const excludedFieldIndexes = ref([])
const fileName = ref('')
// csv export options
const csvTerminatedOpts = ref({ fieldsTerminatedBy: '', linesTerminatedBy: '' })
const csvCheckboxOpts = ref({ noBackslashEscapes: false, withHeaders: false })

const chosenSqlOpt = ref(SQL_EXPORT_OPTS.BOTH)

const fileFormats = computed(() => [
  {
    contentType: 'data:text/csv;charset=utf-8;',
    extension: 'csv',
  },
  {
    contentType: 'data:application/json;charset=utf-8;',
    extension: 'json',
  },
  {
    contentType: 'data:application/sql;charset=utf-8;',
    extension: 'sql',
    disabled: !props.exportAsSQL,
  },
])
const sqlExportOpts = computed(() =>
  Object.keys(SQL_EXPORT_OPTS).map((key) => ({
    text: t(SQL_EXPORT_OPTS[key]),
    value: SQL_EXPORT_OPTS[key],
  }))
)
const selectedFields = computed(() =>
  props.fields.reduce((acc, field, i) => {
    if (!excludedFieldIndexes.value.includes(i)) acc.push(field)
    return acc
  }, [])
)
const totalSelectedFields = computed(() => selectedFields.value.length)
const selectedFieldsLabel = computed(() => {
  if (totalSelectedFields.value > 1)
    return `${selectedFields.value[0]} (+${totalSelectedFields.value - 1} others)`
  return selectedFields.value.join(', ')
})
const selectedFieldsErrMsg = computed(() =>
  totalSelectedFields.value ? '' : t('errors.requiredInput', { inputName: t('fieldsToExport') })
)

watch(isConfigDialogOpened, () => assignDefOpt())

function assignDefOpt() {
  excludedFieldIndexes.value = []
  //TODO: Determine OS newline and store it as user preferences
  // escape reserved single character escape sequences so it can be rendered to the DOM
  csvTerminatedOpts.value = {
    fieldsTerminatedBy: '\\t',
    linesTerminatedBy: '\\n',
  }
  selectedFormat.value = fileFormats.value[0] // csv
}

/**
 * Input entered by the user is escaped automatically.
 * As the result, if the user enters \t, it is escaped as \\t. However, here
 * we allow the user to add the custom line | fields terminator, so when the user
 * enters \t, it should be parsed as a tab character. At the moment, JS doesn't
 * allow to have dynamic escaped char. So this function uses JSON.parse approach
 * to unescaped inputs
 * @param {String} v - users utf8 input
 */
function unescapedUserInput(v) {
  try {
    let str = v
    // if user enters \\, escape it again so it won't be removed when it is parsed by JSON.parse
    if (str.includes('\\\\')) str = escapeForCSV(str)
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
 * @param {(String|Number)} v field value
 * @returns {(String|Number)} returns escape value
 */
function escapeForCSV(v) {
  // NULL is returned as js null in the query result.
  if (typy(v).isNull) return csvCheckboxOpts.value.noBackslashEscapes ? 'NULL' : '\\N' // db escape
  if (typy(v).isString) return v.replace(/\\/g, '\\\\') // replace \ with \\
  return v
}

function escapeForSQL(v) {
  if (typy(v).isNull) return 'NULL'
  if (typy(v).isString) return `'${v.replace(/'/g, "''")}'`
  return v
}

function getValues({ row, escaper }) {
  return row.reduce((acc, field, fieldIdx) => {
    if (!excludedFieldIndexes.value.includes(fieldIdx)) acc.push(escaper(field))
    return acc
  }, [])
}

function buildColDef({ colName, colsMetadataMap }) {
  const { type, length } = colsMetadataMap[colName]
  let tokens = [quotingIdentifier(colName), type]
  if (length) tokens.push(`(${length})`)
  return tokens.join(' ')
}

function genTableCreationScript(identifier) {
  const colsMetadataMap = keyBy(props.metadata, 'name')
  let tokens = ['CREATE TABLE', `${identifier}`, '(']
  selectedFields.value.forEach((colName, i) => {
    tokens.push(
      `${buildColDef({ colName, colsMetadataMap })}${
        i < selectedFields.value.length - 1 ? ',' : ''
      }`
    )
  })
  tokens.push(');')
  return sqlCommenter.genSection('Create') + '\n' + tokens.join(' ')
}

function genInsertionScript(identifier) {
  const fields = selectedFields.value.map((f) => quotingIdentifier(f)).join(', ')
  const insertionSection = `${sqlCommenter.genSection('Insert')}\n`
  if (!props.rows.length) return insertionSection
  return (
    insertionSection +
    `INSERT INTO ${identifier} (${fields}) VALUES` +
    props.rows.map((row) => `(${getValues({ row, escaper: escapeForSQL }).join(',')})`).join(',')
  )
}

function toCsv() {
  const fieldsTerminatedBy = unescapedUserInput(csvTerminatedOpts.value.fieldsTerminatedBy)
  const linesTerminatedBy = unescapedUserInput(csvTerminatedOpts.value.linesTerminatedBy)
  let str = ''
  if (csvCheckboxOpts.value.withHeaders) {
    const fields = selectedFields.value.map((field) => escapeForCSV(field))
    str = `${fields.join(fieldsTerminatedBy)}${linesTerminatedBy}`
  }
  str += props.rows
    .map((row) => getValues({ row, escaper: escapeForCSV }).join(fieldsTerminatedBy))
    .join(linesTerminatedBy)

  return `${str}${linesTerminatedBy}`
}

function toJson() {
  let arr = []
  for (let i = 0; i < props.rows.length; ++i) {
    let obj = {}
    for (const [n, field] of props.fields.entries()) {
      if (!excludedFieldIndexes.value.includes(n)) obj[`${field}`] = props.rows[i][n]
    }
    arr.push(obj)
  }
  return JSON.stringify(arr)
}

function toSql() {
  const { STRUCTURE, DATA } = SQL_EXPORT_OPTS

  const tblNames = uniq(props.metadata.map((item) => item.table))
  // e.g. employees_departments if the resultset is from a join query
  const identifier = quotingIdentifier(tblNames.join('_'))

  let script = ''

  switch (chosenSqlOpt.value) {
    case STRUCTURE:
      script = genTableCreationScript(identifier)
      break
    case DATA:
      script = genInsertionScript(identifier)
      break
    default:
      script = genTableCreationScript(identifier) + '\n' + genInsertionScript(identifier)
      break
  }
  const { content } = sqlCommenter.genHeader()
  return `${content}\n\n${formatSQL(script)}`
}

/**
 * @param {string} fileExtension
 * @return {string}
 */
function getData(fileExtension) {
  switch (fileExtension) {
    case 'json':
      return toJson()
    case 'csv':
      return toCsv()
    case 'sql':
      return toSql()
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
  let a = document.createElement('a')
  a.href = `${contentType},${encodeURIComponent(getData(extension))}`
  a.download = `${fileName.value}.${extension}`
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
}
</script>

<template>
  <TooltipBtn
    class="mr-2"
    size="small"
    :width="36"
    :min-width="'unset'"
    density="comfortable"
    color="primary"
    variant="outlined"
    @click="openConfigDialog"
  >
    <template #btn-content>
      <VIcon size="14" icon="$mdiDownload" />
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
                <label class="field__label text-small-text label-required" for="fields-to-export">
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
                            class="pointer"
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
                <label class="field__label text-small-text" for="file-format">
                  {{ $t('fileFormat') }}
                </label>
                <VSelect
                  v-model="selectedFormat"
                  :items="fileFormats"
                  return-object
                  item-title="extension"
                  item-value="contentType"
                  :rules="[(v) => !!v || $t('errors.requiredInput', { inputName: 'File format' })]"
                  hide-details="auto"
                  id="file-format"
                />
              </VCol>
            </VRow>
            <template v-if="$typy(selectedFormat, 'extension').safeString === 'csv'">
              <VRow class="ma-n1">
                <VCol
                  v-for="(_, key) in csvTerminatedOpts"
                  :key="key"
                  cols="12"
                  md="12"
                  class="pa-1"
                >
                  <LabelField
                    v-model="csvTerminatedOpts[key]"
                    :label="$t(key)"
                    required
                    hide-details="auto"
                  />
                </VCol>
              </VRow>
              <VRow class="mt-3 mx-n1 mb-n1">
                <VCol v-for="(_, key) in csvCheckboxOpts" :key="key" cols="12" class="pa-1">
                  <VCheckbox
                    v-model="csvCheckboxOpts[key]"
                    class="pa-0 ma-0"
                    color="primary"
                    hide-details="auto"
                  >
                    <template #label>
                      <span class="pointer"> {{ $t(key) }} </span>
                      <VTooltip
                        v-if="key === 'noBackslashEscapes'"
                        top
                        transition="slide-y-transition"
                        max-width="400"
                      >
                        <template #activator="{ props }">
                          <VIcon
                            class="ml-1 pointer"
                            size="16"
                            color="info"
                            icon="$mdiInformationOutline"
                            v-bind="props"
                          />
                        </template>
                        {{ $t(`info.${key}`) }}
                      </VTooltip>
                    </template>
                  </VCheckbox>
                </VCol>
              </VRow>
            </template>
            <template v-if="$typy(selectedFormat, 'extension').safeString === 'sql'">
              <VRow class="mt-3 mx-n1 mb-n1">
                <VCol cols="12" md="12" class="pa-1">
                  <label class="field__label text-small-text" for="sql-opt">
                    {{ $t('exportOpt') }}
                  </label>
                  <VTooltip location="top" transition="slide-y-transition">
                    <template #activator="{ props }">
                      <VIcon
                        class="ml-1 pointer"
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
