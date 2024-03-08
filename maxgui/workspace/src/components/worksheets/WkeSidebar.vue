<template>
    <div
        class="sidebar-wrapper d-flex flex-column fill-height mxs-color-helper border-right-table-border"
    >
        <div class="sidebar-toolbar" :class="[isCollapsed ? 'pa-1' : 'pa-3']">
            <div class="d-flex align-center justify-center">
                <span
                    v-if="!isCollapsed"
                    class="mxs-color-helper text-small-text sidebar-toolbar__title d-inline-block text-truncate text-capitalize"
                >
                    {{ title }}
                </span>
                <slot v-if="!isCollapsed" name="collapse-btn-prepend" />
                <mxs-tooltip-btn
                    btnClass="toggle-sidebar"
                    icon
                    small
                    color="primary"
                    @click="isCollapsed = !isCollapsed"
                >
                    <template v-slot:btn-content>
                        <v-icon
                            size="22"
                            class="collapse-icon"
                            :class="[isCollapsed ? 'rotate-right' : 'rotate-left']"
                        >
                            mdi-chevron-double-down
                        </v-icon>
                    </template>
                    {{ isCollapsed ? $mxs_t('expand') : $mxs_t('collapse') }}
                </mxs-tooltip-btn>
            </div>
            <slot v-if="!isCollapsed" name="toolbar-append" />
        </div>
        <slot />
    </div>
</template>

<script>
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
export default {
    name: 'wke-sidebar',
    props: {
        value: { type: Boolean, required: true },
        title: { type: String, required: true },
    },
    computed: {
        isCollapsed: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
    },
}
</script>
<style lang="scss" scoped>
.sidebar-wrapper {
    width: 100%;
    .sidebar-toolbar {
        height: 60px;
        padding-top: 2px !important;
        &__title {
            font-size: 12px;
            margin-right: auto;
        }
    }
}
</style>
