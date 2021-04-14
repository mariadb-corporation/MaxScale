<template>
    <page-wrapper>
        <v-sheet class="pt-6 pb-8 fill-height">
            <page-header />
            <split-pane
                :minPercent="dbPaneMinPercent"
                :defaultPercent="dfDbPanePercent"
                split="vert"
                disable
            >
                <template slot="pane-left">
                    <db-list :dist="dist" class="db-tb-list" @is-collapsed="handleDbListCollapse" />
                </template>
                <template slot="pane-right">
                    <split-pane split="horiz" :minPercent="10" :defaultPercent="70">
                        <template slot="pane-left">
                            <query-editor
                                v-model="value"
                                class="editor pt-2 pl-2"
                                :tableDist="distArr"
                            />
                        </template>
                        <template slot="pane-right">
                            <query-result class="query-result pb-3" />
                        </template>
                    </split-pane>
                </template>
            </split-pane>
        </v-sheet>
    </page-wrapper>
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
import QueryEditor from '@/components/QueryEditor'
import PageHeader from './PageHeader'
import DbList from './DbList'
import QueryResult from './QueryResult'
export default {
    name: 'query-view',
    components: {
        'query-editor': QueryEditor,
        PageHeader,
        DbList,
        QueryResult,
    },
    data() {
        return {
            //TODO: Get data from API
            dist: {}, // contains database name, table name and its columns
            //TODO: Remove this sample sql
            // eslint-disable-next-line vue/max-len
            value: `CREATE TABLE dbo.EmployeePhoto (\n   EmployeeId INT NOT NULL PRIMARY KEY,\n   Photo VARBINARY(MAX) FILESTREAM NULL,\n   MyRowGuidColumn UNIQUEIDENTIFIER NOT NULL ROWGUIDCOL UNIQUE DEFAULT NEWID()\n);\nGO\n/*\n text_of_comment\n /* nested comment */\n* / -- line comment\nEND PRINT 'Too much for the market to bear';\nMERGE INTO Sales.SalesReason AS [ Target ] USING (\n   VALUES\n      ('Recommendation', 'Other'),\n      ('Review', 'Marketing'),\n      ('Internet', 'Promotion')\n) AS [ Source ] ([ NewName ], NewReasonType) ON [ Target ].[ Name ] = [ Source ].[ NewName ]\nWHEN MATCHED THEN\nUPDATE\nSET\n   ReasonType = [ Source ].NewReasonType\n   WHEN NOT MATCHED BY TARGET THEN\nINSERT\n   ([ Name ], ReasonType)\nVALUES\n   ([ NewName ], NewReasonType) OUTPUT $ action INTO @SummaryOfChanges;\nSELECT\n   ProductID,\n   OrderQty,\n   SUM(LineTotal) AS Total\nFROM\n   Sales.SalesOrderDetail\nWHERE\n   UnitPrice < $ 5.00\nGROUP BY\n   ProductID,\n   OrderQty\nORDER BY\n   ProductID,\n   OrderQty OPTION (HASH GROUP, FAST 10);`,
            dbPaneMinPercent: 20,
            dfDbPanePercent: 20,
        }
    },
    computed: {
        distArr: function() {
            let result = []
            //TODO: Flatten dist
            return result
        },
    },
    methods: {
        handleDbListCollapse(v) {
            if (v) this.dfDbPanePercent = 3
            else this.dfDbPanePercent = 20
        },
    },
}
</script>

<style lang="scss" scoped>
.editor,
.db-tb-list,
.query-result {
    border: 1px solid $table-border;
    width: 100%;
    height: 100%;
}
</style>
