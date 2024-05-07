<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
defineOptions({ inheritAttrs: false })
const emit = defineEmits(['get-values'])
const attrs = useAttrs()

const typy = useTypy()

let selectedItems = ref([])
watch(
  selectedItems,
  (v) => {
    // alway emits array
    if (typy(v).isNull) emit('get-values', [])
    else if (typy(v).isArray) emit('get-values', v)
    else emit('get-values', [v])
  },
  { immediate: true, deep: true }
)
</script>

<template>
  <CollapsibleCtr
    class="mt-4"
    titleWrapperClass="mx-n9"
    :title="`${$t(attrs.type, attrs.multiple ? 2 : 1)}`"
  >
    <ObjSelect v-model="selectedItems" v-bind="$attrs" />
  </CollapsibleCtr>
</template>
