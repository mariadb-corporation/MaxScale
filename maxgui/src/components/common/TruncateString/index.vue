<template>
    <v-menu
        v-if="isTruncated"
        offset-y
        top
        transition="slide-y-transition"
        :close-on-content-click="false"
        content-class="shadow-drop color text-navigation truncate-text-menu"
        open-on-hover
        allow-overflow
        :max-width="menuMaxWidth"
        :max-height="menuMaxHeight"
    >
        <!-- TODO: nudge menu list to center -->
        <template v-slot:activator="{ on }">
            <div
                ref="string"
                class="d-inline-block"
                :class="{ 'text-truncate': isTruncated }"
                :style="{ maxWidth: `${maxWidth}px` }"
                v-on="on"
            >
                {{ text }}
            </div>
        </template>
        <pre class="py-2 px-4">{{ text }}</pre>
    </v-menu>
    <div
        v-else
        ref="string"
        class="d-inline-block"
        :class="{ 'text-truncate': isTruncated }"
        :style="{ maxWidth: `${maxWidth}px` }"
    >
        {{ text }}
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'truncate-string',
    props: {
        text: { type: String, required: true },
        maxWidth: {
            validator(value) {
                if (!value) return true
                return typeof value === 'number'
            },
        },

        menuMaxHeight: { type: Number, default: 600 },
        menuMaxWidth: { type: Number, default: 600 },
    },
    data() {
        return {
            isTruncated: true,
        }
    },
    watch: {
        text() {
            this.$help.doubleRAF(() => this.checkTruncated())
        },
        maxWidth() {
            this.$help.doubleRAF(() => this.checkTruncated())
        },
    },
    mounted() {
        this.$help.doubleRAF(() => this.checkTruncated())
    },
    methods: {
        checkTruncated() {
            if (!this.$refs.string) return false
            this.isTruncated = this.$refs.string.scrollWidth > this.$refs.string.clientWidth
        },
    },
}
</script>
<style lang="scss" scoped>
.truncate-text-menu {
    background: $background;
    font-size: 14px;
}
</style>
