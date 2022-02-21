<template>
    <!-- Confirmation type may have different text for save button,
    so it will use `saveText` in $attrs if it's provided-->
    <base-dialog
        v-bind="{ ...$attrs }"
        :saveText="$attrs.saveText ? $attrs.saveText : type"
        v-on="$listeners"
    >
        <template v-slot:form-body>
            <slot name="confirm-text">
                <p
                    v-if="!$help.isNull(item)"
                    class="confirmations-text"
                    v-html="$t(`confirmations.${type}`, { targetId: item.id })"
                ></p>
            </slot>

            <slot name="body-prepend"></slot>
            <small> {{ smallInfo }} </small>
            <slot name="body-append"></slot>
        </template>
        <!-- Pass on all named slots -->
        <slot v-for="slot in Object.keys($slots)" :slot="slot" :name="slot" />
    </base-dialog>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'confirm-dialog',
    inheritAttrs: false,
    props: {
        type: { type: String, required: true }, //check confirmations in en.json
        item: { type: Object, default: null },
        smallInfo: { type: String, default: '' },
    },
}
</script>
