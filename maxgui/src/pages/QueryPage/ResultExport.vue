<template>
    <v-menu
        allow-overflow
        transition="slide-y-transition"
        offset-y
        left
        content-class="mariadb-select-v-menu mariadb-select-v-menu--full-border"
    >
        <template v-slot:activator="{ on, attrs }">
            <v-btn
                x-small
                class="mr-2"
                outlined
                depressed
                color="accent-dark"
                v-bind="attrs"
                v-on="on"
            >
                <v-icon size="14" color="accent-dark">
                    file_download
                </v-icon>
            </v-btn>
        </template>
        <v-list max-width="220px" class="export-file-list">
            <v-list-item
                v-for="format in fileFormats"
                :key="format"
                dense
                link
                @click.native="() => onDownload(format)"
            >
                {{ format }}
            </v-list-item>
        </v-list>
    </v-menu>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'result-data-table',
    props: {
        rows: { type: Array, required: true },
        headers: { type: Array, required: true },
    },
    data() {
        return {
            fileFormats: ['csv', 'xlsx', 'json'],
        }
    },
    methods: {
        onDownload(format) {
            // TODO: convert result data to diffrent formats
            switch (format) {
                case 'csv':
                    break
                case 'xlsx':
                    break
                case 'json':
                    break
            }
        },
    },
}
</script>

<style lang="scss" scoped></style>
