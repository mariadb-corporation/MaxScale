<template>
    <v-list-item class="nav-item-ctr my-2 pointer">
        <div
            class="nav-item d-flex align-center justify-center pa-2"
            :class="{
                'nav-item--active': currentPath.includes(item.path),
            }"
        >
            <v-icon
                class="nav-item__icon"
                :size="item.meta.size"
                :color="currentPath.includes(item.path) ? 'blue-azure' : 'navigation'"
            >
                {{ item.meta.icon }}
            </v-icon>
            <span v-show="!isMini" class="nav-item__label ml-4 text-capitalize text-no-wrap">
                {{
                    item.label === 'dashboards'
                        ? $mxs_tc(`${item.label}`, 1)
                        : $mxs_tc(`${item.label}`, 2)
                }}
            </span>
        </div>
    </v-list-item>
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
    name: 'nav-item',
    props: {
        item: { type: Object, required: true },
        isMini: { type: Boolean, required: true },
        currentPath: { type: String, required: true },
    },
}
</script>

<style lang="scss" scoped>
.nav-item-ctr {
    height: 52px;
    &:hover {
        background: #eefafd !important;
    }
    .nav-item {
        height: 40px;
        &__icon {
            height: 100%;
            margin: 0;
            align-items: center;
            justify-content: center;
        }
        &__label {
            color: $navigation;
            font-size: 1rem;
        }
        &--active {
            background-color: $separator;
            border-radius: 8px;
            .nav-item__label {
                color: $blue-azure;
            }
        }
    }
}
</style>
