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
                :key="format.extension"
                dense
                link
                :href="`${format.href},${encodeURIComponent(getData(format.extension))}`"
                :download="getFileName(format.extension)"
            >
                {{ format.extension }}
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
            fileFormats: [
                {
                    href: 'data:application/json;charset=utf-8;',
                    extension: 'json',
                },
                {
                    href: 'data:application/csv;charset=utf-8;',
                    extension: 'csv',
                },
            ],
        }
    },
    computed: {
        jsonData() {
            let arr = []
            for (let i = 0; i < this.rows.length; ++i) {
                let obj = {}
                for (const [n, header] of this.headers.entries()) {
                    obj[`${header}`] = this.rows[i][n]
                }
                arr.push(obj)
            }
            return JSON.stringify(arr)
        },
        csvData() {
            let str = `${this.headers.join(',')}\n`
            for (let i = 0; i < this.rows.length; ++i) {
                str += `${this.rows[i].join(',')}\n`
            }
            return str
        },
    },
    methods: {
        getData(fileExtension) {
            switch (fileExtension) {
                case 'json':
                    return this.jsonData
                case 'csv':
                    return this.csvData
            }
        },
        getFileName(fileExtension) {
            return `MaxScale Query Results - ${this.$help.dateFormat({
                value: new Date(),
                formatType: 'DATE_RFC2822',
            })}.${fileExtension}`
        },
    },
}
</script>
