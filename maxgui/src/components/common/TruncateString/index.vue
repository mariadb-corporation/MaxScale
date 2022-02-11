<template>
    <v-menu
        v-if="isTruncated"
        offset-y
        top
        transition="slide-y-transition"
        :close-on-content-click="false"
        content-class="shadow-drop"
        open-on-hover
        :nudge-left="nudgeLeft"
        :nudgeTop="nudgeTop"
        :max-width="menuMaxWidth"
        :max-height="menuMaxHeight"
        :disabled="disabled"
    >
        <!-- TODO: nudge menu list to center -->
        <template v-slot:activator="{ on }">
            <div
                ref="string"
                class="d-inline-block"
                :class="[isTruncated ? 'text-truncate pointer' : '', activatorClass]"
                :style="{ maxWidth: `${getMaxWidth}px` }"
                v-on="on"
            >
                <slot> {{ text }} </slot>
            </div>
        </template>
        <v-sheet :class="['truncate-string-menu-content', contentClass]">
            <pre v-if="!wrap" class="truncated-text-pre color text-navigation">{{ text }}</pre>
            <div v-else class="text-body-2 color text-navigation">{{ text }}</div>
        </v-sheet>
    </v-menu>
    <div
        v-else
        ref="string"
        class="d-inline-block"
        :class="{ 'text-truncate': isTruncated }"
        :style="{ maxWidth: `${getMaxWidth}px` }"
    >
        <slot> {{ text }} </slot>
    </div>
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
    name: 'truncate-string',
    props: {
        text: { type: String, required: true },
        /* if maxWidth isn't provided, eleMaxWidth will be used.
         * Notice, eleMaxWidth gets parent width by using clientWidth.
         * That means when box-sizing: border-box, eleMaxWidth will account
         * for any border and padding.
         */
        maxWidth: { type: Number },
        menuMaxHeight: { type: Number, default: 600 },
        menuMaxWidth: { type: Number, default: 600 },
        nudgeLeft: { type: Number, default: 0 },
        nudgeTop: { type: Number, default: 0 },
        disabled: { type: Boolean, default: false },
        wrap: { type: Boolean, default: false },
        activatorClass: { type: String, default: '' },
        contentClass: { type: String, default: 'py-2 px-4' },
    },
    data() {
        return {
            isTruncated: true,
            eleMaxWidth: null,
        }
    },
    computed: {
        getMaxWidth() {
            return this.maxWidth || this.eleMaxWidth
        },
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
        // wait for DOM to render completely
        this.$help.doubleRAF(() => this.checkTruncated())
    },
    methods: {
        checkTruncated() {
            if (!this.$refs.string) return false
            this.eleMaxWidth = this.$refs.string.parentElement.clientWidth
            this.isTruncated = this.$refs.string.scrollWidth > this.$refs.string.clientWidth
        },
    },
}
</script>
<style lang="scss" scoped>
.truncate-string-menu-content {
    overflow: auto;
}
.truncated-text-pre {
    font-size: 14px;
}
</style>
