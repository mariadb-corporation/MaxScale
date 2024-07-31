<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { FK_SUPPORTED_ENGINE } from '@/constants/workspace'
import FkDefinitions from '@wsComps/TblStructureEditor/FkDefinitions.vue'

defineOptions({ inheritAttrs: false })
defineProps({ engine: { type: String, required: true } })
</script>
<template>
  <FkDefinitions v-if="engine === FK_SUPPORTED_ENGINE" v-bind="$attrs" />
  <span v-else class="d-inline-block grayed-out-info pt-3">
    {{ $t('info.fkEngineSupport') }}
  </span>
</template>
