<template>
    <v-menu
        v-bind="$attrs"
        :value="$attrs.activator ? isOpened : menuOpen"
        content-class="v-menu--mariadb v-menu--mariadb-full-border"
        :close-on-content-click="false"
        min-width="auto"
        v-on="$listeners"
    >
        <template v-if="!$attrs.activator" v-slot:activator="{ on }">
            <v-list-item
                v-if="submenuProps.isSubMenu"
                class="d-flex justify-space-between cursor-default"
                v-on="on"
            >
                <v-list-item-title class="mxs-color-helper text-text">
                    {{ submenuProps.text }}
                </v-list-item-title>
                <v-icon size="24" color="primary">
                    mdi-menu-right
                </v-icon>
            </v-list-item>
            <div v-else v-on="on" @click="$attrs.activator ? (isOpened = true) : (menuOpen = true)">
                <slot name="activator">
                    <v-list-item link dense>
                        <v-list-item-title class="mxs-color-helper text-text">
                            {{ submenuProps.text }}
                        </v-list-item-title>
                    </v-list-item>
                </slot>
            </div>
        </template>
        <v-list>
            <template v-for="(item, index) in items">
                <v-divider v-if="item.divider" :key="index" />
                <mxs-sub-menu
                    v-else-if="item.children"
                    :key="`mxs-sub-menu-${index}`"
                    :items="item.children"
                    :submenuProps="{
                        isSubMenu: true,
                        text: item.text,
                        nestedMenuTransition: 'scale-transition',
                        nestedMenuOpenDelay: 150,
                    }"
                    offset-x
                    :offset-y="false"
                    :transition="submenuProps.nestedMenuTransition"
                    :open-delay="submenuProps.nestedMenuOpenDelay"
                    :open-on-hover="true"
                    @item-click="emitClickEvent"
                />
                <v-list-item
                    v-else
                    :key="`v-list-item-${index}`"
                    dense
                    link
                    :disabled="item.disabled"
                    data-test="child-menu-item"
                    @click="emitClickEvent(item)"
                >
                    <v-list-item-title class="mxs-color-helper text-text">
                        {{ item.text }}
                    </v-list-item-title>
                </v-list-item>
            </template>
        </v-list>
    </v-menu>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    inheritAttrs: false,
    props: {
        items: { type: Array, required: true },
        submenuProps: {
            type: Object,
            default: () => ({
                isSubMenu: false,
                text: '',
                nestedMenuTransition: 'scale-transition',
                nestedMenuOpenDelay: 150,
            }),
        },
    },
    data() {
        return {
            menuOpen: false,
        }
    },
    computed: {
        // use this to control menu visibility when using activator
        isOpened: {
            get() {
                return this.$attrs.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
    },
    methods: {
        emitClickEvent(item) {
            this.$emit('item-click', item)
            if (this.$attrs.activator) this.isOpened = false
            else this.menuOpen = false
        },
    },
}
</script>
