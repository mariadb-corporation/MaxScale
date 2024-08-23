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
import { ODBC_DB_TYPES } from '@/constants/workspace'

const props = defineProps({ drivers: { type: Array, required: true } })
const emit = defineEmits(['get-form-data'])
const { t } = useI18n()

const isAdvanced = ref(false)

const form = ref({
  type: null,
  timeout: 30,
  driver: null,
  server: '',
  port: '',
  user: '',
  password: '',
  db: '',
  connection_string: '',
})

const isGeneric = computed(() => form.value.type === 'generic')
const isDbNameRequired = computed(() =>
  form.value.type === 'postgresql' || isGeneric.value ? true : false
)
const generatedConnStr = computed(() => {
  const { driver = '', server = '', port = '', user = '', password = '', db = '' } = form.value
  return genConnStr({ driver, server, port, user, password, db })
})
const driverErrMsgs = computed(() => (props.drivers.length ? '' : t('errors.noDriversFound')))
const dbNameLabel = computed(() => (isGeneric.value ? t('catalog') : t('database')))
const dbNameErrMsg = computed(() =>
  isGeneric.value ? t('errors.requiredCatalog') : t('errors.requiredDb')
)

watch(generatedConnStr, (v) => (form.value.connection_string = v), { immediate: true })
watch(form, (v) => emit('get-form-data', v), { deep: true, immediate: true })

const requiredRule = [(v) => !!v || t('errors.requiredField')]

/**
 * @param {string} param.driver
 * @param {string} param.server
 * @param {string} param.port
 * @param {string} param.user
 * @param {string} param.password
 * @param {string} [param.db] - required if driver is PostgreSQL
 * @returns {string}  ODBC connection_string
 */
function genConnStr({ driver, server, port, user, password, db }) {
  let connStr = `DRIVER=${driver};`
  connStr += `SERVER=${server};PORT=${port};UID=${user};PWD={${password}}`
  if (db) connStr += `;DATABASE=${db}`
  return connStr
}
</script>

<template>
  <VRow class="ma-n1">
    <slot name="prepend" />
    <VCol cols="12" md="6" class="pa-1">
      <label class="label-field text-small-text label--required" for="database-type-dropdown">
        {{ $t('databaseType') }}
      </label>
      <VSelect
        id="database-type-dropdown"
        v-model="form.type"
        :items="ODBC_DB_TYPES"
        item-title="text"
        item-value="id"
        :placeholder="$t('selectDbType')"
        :rules="requiredRule"
        hide-details="auto"
        data-test="database-type-dropdown"
      />
    </VCol>
    <VCol cols="12" md="6" class="pa-1">
      <TimeoutInput v-model="form.timeout" />
    </VCol>
    <VCol cols="12" md="6" class="pa-1">
      <label class="label-field text-small-text label--required" for="driver-dropdown">
        {{ $t('driver') }}
      </label>
      <VSelect
        id="driver-dropdown"
        v-model="form.driver"
        :items="drivers"
        item-title="id"
        item-value="id"
        :placeholder="$t('selectOdbcDriver')"
        :rules="requiredRule"
        hide-details="auto"
        :disabled="isAdvanced"
        :error-messages="driverErrMsgs"
        data-test="driver-dropdown"
      />
    </VCol>
    <VCol cols="12" md="6" class="pa-1" data-test="database-name">
      <LabelField
        v-model="form.db"
        :label="dbNameLabel"
        :required="isDbNameRequired"
        :customErrMsg="dbNameErrMsg"
        :disabled="isAdvanced"
      />
    </VCol>
    <VCol cols="12" md="6" class="pa-1" data-test="hostname">
      <LabelField
        v-model="form.server"
        :label="$t('hostname/IP')"
        required
        :disabled="isAdvanced"
      />
    </VCol>
    <VCol cols="12" md="6" class="pa-1" data-test="port">
      <LabelField v-model="form.port" :label="$t('port')" required :disabled="isAdvanced" />
    </VCol>
    <VCol cols="12" md="6" class="pa-1">
      <UidInput v-model="form.user" :disabled="isAdvanced" name="odbc--uid" />
    </VCol>
    <VCol cols="12" md="6" class="pa-1">
      <PwdInput v-model="form.password" :disabled="isAdvanced" name="odbc--pwd" />
    </VCol>
    <VCol cols="12" md="6" class="pa-1">
      <VSwitch v-model="isAdvanced" :label="$t('advanced')" class="pt-3" hide-details />
    </VCol>
    <VCol v-if="isAdvanced" cols="12" class="pa-1">
      <label class="label-field text-small-text label--required" for="connection_string">
        {{ $t('connStr') }}
      </label>
      <VTextarea
        id="connection_string"
        v-model="form.connection_string"
        auto-grow
        rows="1"
        row-height="15"
        :disabled="!isAdvanced"
        :rules="requiredRule"
      />
    </VCol>
  </VRow>
</template>
